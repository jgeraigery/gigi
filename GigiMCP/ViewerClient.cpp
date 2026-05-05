///////////////////////////////////////////////////////////////////////////////
//         Gigi Rapid Graphics Prototyping and Code Generation Suite         //
//        Copyright (c) 2026 Electronic Arts Inc. All rights reserved.       //
///////////////////////////////////////////////////////////////////////////////

#include "ViewerClient.h"

#include <stdio.h>
#include <string>

#pragma comment(lib, "Ws2_32.lib")

CViewerClient::CViewerClient()
{

}

CViewerClient::~CViewerClient()
{
    Shutdown();
}

int CViewerClient::Start(const std::string& serverIP, const std::string& serverPort)
{
    // Initialize winsock
    int result = WSAStartup(MAKEWORD(2, 2), &m_wsaData);
    if (result != 0)
        return result;

    // get our address info
    addrinfo* resulta = nullptr;
    addrinfo hints;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Resolve the server address and port
    result = getaddrinfo(serverIP.c_str(), serverPort.c_str(), &hints, &resulta);
    if (result != 0) {
        WSACleanup();
        return result;
    }

    // Attempt to connect to an address until one succeeds
    for (addrinfo* ptr = resulta; ptr != nullptr; ptr = ptr->ai_next) {

        // Create a SOCKET for connecting to server
        m_serverSocket = socket(ptr->ai_family, ptr->ai_socktype,ptr->ai_protocol);
        if (m_serverSocket == INVALID_SOCKET) {
            result = WSAGetLastError();
            WSACleanup();
            return result;
        }

        // Connect to server.
        result = connect(m_serverSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (result == SOCKET_ERROR) {
            closesocket(m_serverSocket);
            m_serverSocket = INVALID_SOCKET;
            continue;
        }
        break;
    }

    freeaddrinfo(resulta);

    if (m_serverSocket == INVALID_SOCKET) {
        WSACleanup();
        return 1;
    }

    // make this socket non blocking
    unsigned long ul = 1;
    result = ioctlsocket(m_serverSocket, FIONBIO, (unsigned long*)&ul);
    if (result == SOCKET_ERROR) {
        result = WSAGetLastError();
        closesocket(m_serverSocket);
        WSACleanup();
        return result;
    }

    // we are started and everything is ok!
    m_connected = true;
    return 0;
}

void CViewerClient::Shutdown()
{
    if (!m_connected)
        return;

    if (m_serverSocket != INVALID_SOCKET)
    {
        shutdown(m_serverSocket, SD_SEND);
        closesocket(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
    }

    WSACleanup();
    m_connected = false;

    m_messageRcvdPartial = "";
    std::queue<std::string> empty;
    std::swap(m_messagesRcvd, empty);
}

bool CViewerClient::Tick()
{
    if (!m_connected)
        return true;

    // Try to recieve data from the server socket
    char buffer[1025]; // an extra so we can put a null at the end to strcpy the last bit more easily.
    int bytesRead = recv(m_serverSocket, buffer, 1024, 0);
    if (bytesRead == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSAEWOULDBLOCK)
        {
            Shutdown();
            return false;
        }
    }
    else
    {
        // ensure null termination
        buffer[bytesRead] = 0;

        // process the data we received
        int readStart = 0;
        int readEnd = 0;
        while (readEnd < bytesRead)
        {
            if (buffer[readEnd] == 0)
            {
                std::string_view newPart(&buffer[readStart], readEnd - readStart);

                std::string newMessage = m_messageRcvdPartial + std::string(newPart);

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

bool CViewerClient::Send(const char* msg)
{
    if (!m_connected || m_serverSocket == INVALID_SOCKET)
        return false;

    int result = send(m_serverSocket, msg, (int)strlen(msg) + 1, 0);
    if (result == SOCKET_ERROR)
    {
        Shutdown();
        return false;
    }

    return true;
}
