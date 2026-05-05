///////////////////////////////////////////////////////////////////////////////
//         Gigi Rapid Graphics Prototyping and Code Generation Suite         //
//        Copyright (c) 2026 Electronic Arts Inc. All rights reserved.       //
///////////////////////////////////////////////////////////////////////////////

#pragma once

#define NOMINMAX
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>

#include <queue>
#include <string>
#include <chrono>
// clang-format on

class CViewerServer
{
public:
    CViewerServer();
	~CViewerServer();

	bool Start(int port);
	void Shutdown();

	bool Tick();

    bool PopMessage(std::string& message)
	{
        if (!m_connected)
			return false;

        if (m_messagesRcvd.empty())
            return false;

        message = m_messagesRcvd.front();
        m_messagesRcvd.pop();

        return true;
	}

	bool Send(const char* message);

	bool IsConnected() const { return m_connected; }

private:
    bool m_connected = false;

	WSADATA m_wsaData;
	SOCKET m_listenSocket = INVALID_SOCKET;
	SOCKET m_clientSocket = INVALID_SOCKET;

	std::string m_messageRcvdPartial;
	std::queue<std::string> m_messagesRcvd;
};