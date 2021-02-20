#pragma once

#include "net_common.h"
#include "net_message.h"
#include "net_tsqueue.h"
#include "net_connection.h"

namespace asr
{
	namespace net
	{
		template <typename T>
		class client_interface
		{
		public:
			client_interface()
			{
				// Initialise the socket with the io context
			}

			virtual ~client_interface()
			{
				// If the client is destroyed disconnect from the server
				Disconnect();
			}

		public:
			// Connect to server with hostname/ip-address and port
			bool Connect(const std::string& host, const uint16_t port)
			{
				try
				{
					// Resolve hostname/ip-address into physical address
					asio::ip::tcp::resolver resolver(m_context);
					asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(host, std::to_string(port));

					// Create connection
					m_connection = std::make_unique<connection<T>>(
						connection<T>::owner::client,
						m_context,
						asio::ip::tcp::socket(m_context),
						m_qMessagesIn
						);

					// Tell the connection to connect to the server
					m_connection->ConnectToServer(endpoints);

					// Start context thread
					thrContext = std::thread([this]() {m_context.run(); });
				}
				catch (std::exception& e)
				{
					std::cerr << "Client Exception: " << e.what() << "\n";
					return false;
				}

				return true;
			}

			// Disconnect from server
			void Disconnect()
			{
				// If connection exists and is connected
				if (IsConnected())
				{
					m_connection->Disconnect();
				}

				// Stop the context and its threads
				m_context.stop();
				if (thrContext.joinable())
					thrContext.join();

				// Destroy the connection object
				m_connection.release();
			}

			// Chceks if client is connected to a server
			bool IsConnected()
			{
				if (m_connection)
					return m_connection->IsConnected();
				else
					return false;
			}

		public:
			// Send a message to the server
			void Send(const message<T>& msg)
			{
				m_connection->Send(msg);
			}

			// Retrieve queue of messages from sever
			tsqueue<owned_message<T>>& Incoming()
			{
				return m_qMessagesIn;
			}

		protected:
			// Asio context handles data transfer
			asio::io_context m_context;
			// Thread to execute its work commands
			std::thread thrContext;
			// Single connection object which handles data transfer
			std::unique_ptr<connection<T>> m_connection;

		private:
			// Thread safe queue of incoming messages from server
			tsqueue<owned_message<T>> m_qMessagesIn;
		};
	}
}