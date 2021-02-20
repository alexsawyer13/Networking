#pragma once

#include "net_common.h"
#include "net_tsqueue.h"
#include "net_message.h"
#include "net_connection.h"

namespace asr
{
	namespace net
	{
		template<typename T>
		class server_interface
		{
		public:
			server_interface(uint16_t port)
				: m_asioAcceptor(m_asioContext, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
			{

			}

			virtual ~server_interface()
			{
				Stop();
			}

			bool Start()
			{
				try
				{
					// Give the context work before running so it doesn't immediately close
					WaitForClientConnection();

					m_threadContext = std::thread([this]() {m_asioContext.run(); });
				}
				catch (std::exception& e)
				{
					// Something prevented the server from listening
					std::cerr << "[SERVER] Exception: " << e.what() << "\n";
					return false;
				}

				std::cout << "[SERVER] Started!\n";
				return true;
			}

			void Stop()
			{
				// Request the context to close
				m_asioContext.stop();

				// Tidy up the context thread
				if (m_threadContext.joinable())
					m_threadContext.join();

				std::cout << "[SERVER] Stopped!\n";
			}

			// ASYNC - Instruct asio to wait for connection
			void WaitForClientConnection()
			{
				m_asioAcceptor.async_accept(
					[this](std::error_code ec, asio::ip::tcp::socket socket)
					{
						if (!ec)
						{
							std::cout << "[SERVER] New Connection: " << socket.remote_endpoint() << "\n";

							std::shared_ptr<connection<T>> newconn =
								std::make_shared<connection<T>>(connection<T>::owner::server,
									m_asioContext, std::move(socket), m_qMessagesIn);

							// Give the user a chance to deny connection
							if (OnClientConnect(newconn))
							{
								//Pushes allowed connection to the container of connections
								m_deqConnections.push_back(std::move(newconn));

								m_deqConnections.back()->ConnectToClient(this, nIDCounter++);

								std::cout << "[" << m_deqConnections.back()->GetID() << "] Connection Approved\n";
							}
							else
							{
								std::cout << "[-----] Connection Denied!\n";
							}
						}
						else
						{
							// Error has occured while accepting connection
							std::cout << "[SERVER] New Connection Error: " << ec.message() << "\n";
						}
						
						// Wait for another connection
						WaitForClientConnection();
					}
				);
			}

			// Send a message to a client
			void MessageClient(std::shared_ptr<connection<T>> client, const message<T>& msg)
			{
				if (client && client->IsConnected())
				{
					client->Send(msg);
				}
				else
				{
					// Assume client has disconnected
					OnClientDisconnect(client);
					client.reset();
					m_deqConnections.erase(
						std::remove(m_deqConnections.begin(), m_deqConnections.end(), client), m_deqConnections.end());
				}
			}

			void MessageAllClients(const message<T>& msg, std::shared_ptr<connection<T>> pIgnoreClient = nullptr)
			{
				bool bInvalidClientExists = false;

				for (auto& client : m_deqConnections)
				{
					// Check if client is connected
					if (client && client->IsConnected())
					{
						if (client != pIgnoreClient)
						{
							client->Send(msg);
						}
					}
					else
					{
						// Client couldn't be contacted so assume it has disconnected
						OnClientDisconnect(client);
						client.reset();
						bInvalidClientExists = true;
					}
				}

				// Removes all invalid clients
				if (bInvalidClientExists)
				{
					m_deqConnections.erase(
						std::remove(m_deqConnections.begin(), m_deqConnections.end(), nullptr), m_deqConnections.end());
				}
			}

			// Processes up to nMaxMessages messages in the queue, defaults to max size_t
			void Update(size_t nMaxMessages = -1, bool bWait = false)
			{
				if (bWait)
					m_qMessagesIn.wait();

				size_t nMessageCount = 0;
				while (nMessageCount < nMaxMessages && !m_qMessagesIn.empty())
				{
					// Take the front message
					auto msg = m_qMessagesIn.pop_front();

					// Handle the message
					OnMessage(msg.remote, msg.msg);

					nMessageCount++;
				}
			}

		protected:
			// Called when a client connects, return false to reject connection
			virtual bool OnClientConnect(std::shared_ptr<connection<T>> client)
			{
				return false;
			}

			// Called when a client has disconnected
			virtual void OnClientDisconnect(std::shared_ptr<connection<T>> client)
			{

			}

			// Called when a message arrives
			virtual void OnMessage(std::shared_ptr<connection<T>> client, message<T>& msg)
			{

			}

		public:
			// called when a client is validated
			virtual void OnClientValidated(std::shared_ptr<connection<T>> client)
			{

			}

		protected:
			// Thread Safe Queue for incoming messages
			tsqueue<owned_message<T>> m_qMessagesIn;

			// Container of activate validated connections
			std::deque<std::shared_ptr<connection<T>>> m_deqConnections;

			// ASIO Context and thread
			asio::io_context m_asioContext;
			std::thread m_threadContext;

			// These things need an asio context
			asio::ip::tcp::acceptor m_asioAcceptor;

			// Clients will be identified via an ID
			uint32_t nIDCounter = 10000;
		};
	}
}