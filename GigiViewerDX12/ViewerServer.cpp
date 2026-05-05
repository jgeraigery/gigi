///////////////////////////////////////////////////////////////////////////////
//         Gigi Rapid Graphics Prototyping and Code Generation Suite         //
//        Copyright (c) 2026 Electronic Arts Inc. All rights reserved.       //
///////////////////////////////////////////////////////////////////////////////

#include "ViewerServer.h"

#include <stdio.h>

#pragma comment(lib, "Ws2_32.lib")

static void RemoveTelnetCommands(std::string& data)
{
    std::string cleaned;
    cleaned.reserve(data.size());

    for (size_t i = 0; i < data.size(); i++)
    {
        unsigned char byte = static_cast<unsigned char>(data[i]);

        if (byte == 0xFF) // IAC (Interpret As Command)
        {
            if (i + 1 < data.size())
            {
                unsigned char next = static_cast<unsigned char>(data[i + 1]);

                if (next == 0xFF)
                {
                    // IAC IAC = literal 0xFF byte
                    cleaned += static_cast<char>(0xFF);
                    i += 1;  // Skip both bytes
                }
                else if (next >= 0xFB && next <= 0xFE)
                {
                    // WILL (0xFB), WONT (0xFC), DO (0xFD), DONT (0xFE)
                    // These are 3-byte sequences: IAC + command + option
                    i += 2;  // Skip all 3 bytes (i++ at end of loop adds 1)
                }
                else if (next == 0xFA)
                {
                    // SB (subnegotiation begin) - scan until IAC SE (0xFF 0xF0)
                    i += 1;
                    while (i + 1 < data.size())
                    {
                        if (static_cast<unsigned char>(data[i]) == 0xFF &&
                            static_cast<unsigned char>(data[i + 1]) == 0xF0)
                        {
                            i += 1;  // Skip IAC SE
                            break;
                        }
                        i++;
                    }
                }
                else
                {
                    // Other 2-byte commands (NOP, DM, BRK, IP, AO, AYT, EC, EL, GA)
                    i += 1;  // Skip both bytes
                }
            }
        }
        else
        {
            // Normal data byte
            cleaned += data[i];
        }
    }

    data = std::move(cleaned);
}

CViewerServer::CViewerServer()
{

}

CViewerServer::~CViewerServer()
{
    Shutdown();
}

bool CViewerServer::Start(int port)
{
    // Initialize winsock
	int result = WSAStartup(MAKEWORD(2, 2), &m_wsaData);
	if (result != 0)
		return false;

    // get our address info
    addrinfo* resulta = nullptr;
    addrinfo* ptr = nullptr;
    addrinfo hints;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    // Resolve the local address and port to be used by the server
    char portString[256];
    sprintf_s(portString, "%i", port);
    result = getaddrinfo(NULL, portString, &hints, &resulta);
    if (result != 0) {
        WSACleanup();
        return false;
    }

    // Create a SOCKET for the server to listen for client connections
    m_listenSocket = socket(resulta->ai_family, resulta->ai_socktype, resulta->ai_protocol);
    if (m_listenSocket == INVALID_SOCKET) {
        result = WSAGetLastError();
        freeaddrinfo(resulta);
        WSACleanup();
        return false;
    }

    // make this socket non blocking
    unsigned long ul = 1;
    result = ioctlsocket(m_listenSocket, FIONBIO, (unsigned long*)&ul);
    if (result == SOCKET_ERROR) {
        result = WSAGetLastError();
        freeaddrinfo(resulta);
        closesocket(m_listenSocket);
        WSACleanup();
        return false;
    }

    // Setup the TCP listening socket
    result = bind(m_listenSocket, resulta->ai_addr, (int)resulta->ai_addrlen);
    if (result == SOCKET_ERROR) {
        result = WSAGetLastError();
        freeaddrinfo(resulta);
        closesocket(m_listenSocket);
        WSACleanup();
        return false;
    }
    freeaddrinfo(resulta);

    // listen
    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        result = WSAGetLastError();
        closesocket(m_listenSocket);
        WSACleanup();
        return false;
    }

    // we are started and everything is ok!
    m_connected = true;
	return true;
}

void CViewerServer::Shutdown()
{
    if (!m_connected)
        return;

    if (m_listenSocket != INVALID_SOCKET)
    {
        shutdown(m_listenSocket, SD_SEND);
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }

    if (m_clientSocket != INVALID_SOCKET)
    {
        shutdown(m_clientSocket, SD_SEND);
        closesocket(m_clientSocket);
        m_clientSocket = INVALID_SOCKET;
    }

    WSACleanup();
    m_connected = false;

    m_messageRcvdPartial = "";
    std::queue<std::string> empty;
    std::swap(m_messagesRcvd, empty);
}

bool CViewerServer::Tick()
{
    if (!m_connected)
        return true;

    // If we don't yet have a connection, try to get one
    if (m_clientSocket == INVALID_SOCKET)
    {
        m_clientSocket = accept(m_listenSocket, nullptr, nullptr);
        if (m_clientSocket == INVALID_SOCKET)
        {
            if (WSAGetLastError() != WSAEWOULDBLOCK)
            {
                Shutdown();
                return false;
            }
            return true;
        }
    }

    // Try to recieve data from the client socket
    char buffer[1025]; // an extra so we can put a null at the end to strcpy the last bit more easily.
    int bytesRead = recv(m_clientSocket, buffer, 1024, 0);
    if (bytesRead == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSAEWOULDBLOCK)
        {
            closesocket(m_clientSocket);
            m_clientSocket = INVALID_SOCKET;
            return false;
        }
    }
    else if (bytesRead == 0)
    {
        // 0 bytes recieved mean client disconnected
        closesocket(m_clientSocket);
        m_clientSocket = INVALID_SOCKET;
    }
    else if (bytesRead > 0)
    {
        // ensure null termination
        buffer[bytesRead] = 0;

        // process the data we received
        int readStart = 0;
        int readEnd = 0;
        while (readEnd < bytesRead)
        {
            if (buffer[readEnd] == 0 || buffer[readEnd] == '\r' || buffer[readEnd] == '\n')
            {
                std::string_view newPart(&buffer[readStart], readEnd - readStart);

                std::string newMessage = m_messageRcvdPartial + std::string(newPart);

                // Remove any telnet commands (3 bytes, starting with 0xFF) in case people use a telnet client, instead of a raw client.
                RemoveTelnetCommands(newMessage);

                if (!newMessage.empty())
                    m_messagesRcvd.push(newMessage);

                m_messageRcvdPartial = "";

                readEnd += 1;
                readStart = readEnd;
            }
            else
            {
                readEnd++;
            }
        }

        if (readStart < bytesRead)
        {
            buffer[bytesRead] = 0;
            m_messageRcvdPartial += &buffer[readStart];
        }
    }

    return true;
}

bool CViewerServer::Send(const char* msg)
{
    if (!m_connected || m_clientSocket == INVALID_SOCKET)
        return true;

    int result = send(m_clientSocket, msg, (int)strlen(msg) + 1, 0);
    if (result == SOCKET_ERROR)
    {
        Shutdown();
        return false;
    }

    return true;
}
