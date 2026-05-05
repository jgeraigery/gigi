///////////////////////////////////////////////////////////////////////////////
//         Gigi Rapid Graphics Prototyping and Code Generation Suite         //
//        Copyright (c) 2026 Electronic Arts Inc. All rights reserved.       //
///////////////////////////////////////////////////////////////////////////////

#pragma once

// clang-format off
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX 1
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <queue>
#include <string>
#include <stdarg.h>
// clang-format on

class CViewerClient
{
public:
    CViewerClient();
	~CViewerClient();

	int Start(const std::string& serverIP, const std::string& serverPort);
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

	bool Send(const char* msg);

    bool IsConnected() const { return m_connected; }

private:
    bool m_connected = false;
	WSADATA m_wsaData;
	SOCKET m_serverSocket = INVALID_SOCKET;

	std::string m_messageRcvdPartial;
	std::queue<std::string> m_messagesRcvd;
};