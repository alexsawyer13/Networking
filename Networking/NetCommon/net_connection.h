#pragma once

#include "net_common.h"
#include "net_tsqueue.h"
#include "net_message.h"

namespace asr
{
	namespace net
	{
		// Forward declare server interface
		template<typename T>
		class server_interface;

		template<typename T>
		class connection : public std::enable_shared_from_this<connection<T>>
		{
		public:
			enum class owner
			{
				server,
				client
			};

			connection(owner parent, asio::io_context& asioContext, asio::ip::tcp::socket socket, tsqueue<owned_message<T>>& qIn)
				: m_asioContext(asioContext), m_socket(std::move(socket)), m_qMessagesIn(qIn)
			{
				m_nOwnerType = parent;

				// Construct validation check data
				if (m_nOwnerType == owner::server)
				{
					// Construct random data for the client validation
					m_nHandshakeOut = uint64_t(std::chrono::system_clock::now().time_since_epoch().count());

					// Calcualte the scrambled result
					m_nHandshakeCheck = scramble(m_nHandshakeOut);
				}
				else
				{
					// Client doesn't have to do anything yet
					m_nHandshakeIn = 0;
					m_nHandshakeOut = 0;
				}
			}

			virtual ~connection()
			{}

			uint32_t GetID() const
			{
				return id;
			}

		public:
			void ConnectToClient(asr::net::server_interface<T>* server, uint32_t uid = 0)
			{
				// Only servers can connect to clients
				if (m_nOwnerType == owner::server)
				{
					// If the socket is open
					if (m_socket.is_open())
					{
						// Gives the client a uid and primes asio to read a header
						id = uid;

						// Send validation packet to client
						WriteValidation();

						// Asynchronously wait for validation packet to be received
						ReadValidation(server);

						//ReadHeader();
					}
				}
			}

			void ConnectToServer(const asio::ip::tcp::resolver::results_type& endpoints)
			{
				// Only clients can connect to servers
				if (m_nOwnerType == owner::client)
				{
					// Primes asio to attempt to connect to an endpoint
					asio::async_connect(m_socket, endpoints, 
						[this](std::error_code ec, asio::ip::tcp::endpoint endpoint)
						{
							if (!ec)
							{
								// Wait for server to send validation packet
								ReadValidation();

								// Primes asio to read headers from the server
								//ReadHeader();
							}
						}
					);
				}
			}

			void Disconnect()
			{
				if (IsConnected())
					asio::post(m_asioContext, [this]() {m_socket.close(); });
			}

			bool IsConnected() const
			{
				return m_socket.is_open();
			}

		public:
			void Send(const message<T>& msg)
			{
				asio::post(m_asioContext,
					[this, msg]()
					{
						// If the messages out queue isn't empty then asio is handling it already
						bool bWritingMessage = !m_qMessagesOut.empty();
						m_qMessagesOut.push_back(msg);

						// Only give a WriteHeader() workload if it's not already writing messages
						if (!bWritingMessage)
							WriteHeader();
					}
				);
			}

		private:
			// ASYNC - Prime context to read a message header
			void ReadHeader()
			{
				asio::async_read(m_socket, asio::buffer(&m_msgTemporaryIn.header, sizeof(message_header<T>)),
					[this](std::error_code ec, std::size_t length)
					{
						if (!ec)
						{
							if (m_msgTemporaryIn.header.size > 0)
							{
								m_msgTemporaryIn.body.resize(m_msgTemporaryIn.header.size);
								ReadBody();
							}
							else
							{
								AddToIncomingMessageQueue();
							}
						}
						else
						{
							std::cout << "[" << id << "] Read header fail.\n";
							m_socket.close();
						}
					}
					);
			}

			// ASYNC - Prime context to read a message body
			void ReadBody()
			{
				asio::async_read(m_socket, asio::buffer(m_msgTemporaryIn.body.data(), m_msgTemporaryIn.header.size),
					[this](std::error_code ec, std::size_t length)
					{
						if (!ec)
						{
							AddToIncomingMessageQueue();
						}
						else
						{
							std::cout << "[" << id << "] Read body fail.\n";
							m_socket.close();
						}
					}
					);
			}

			// ASYNC - Prime context to write a message header
			void WriteHeader()
			{
				asio::async_write(m_socket, asio::buffer(&m_qMessagesOut.front().header, sizeof(message_header<T>)), 
					[this](std::error_code ec, std::size_t length)
					{
						if (!ec)
						{
							if (m_qMessagesOut.front().header.size > 0)
							{
								WriteBody();
							}
							else
							{
								m_qMessagesOut.pop_front();

								if (!m_qMessagesOut.empty())
								{
									WriteHeader();
								}
							}
						}
						else
						{
							std::cout << "[" << id << "] Write header fail.\n";
							m_socket.close();
						}
					}
					);
			}

			// ASYNC - Prime context to write a message body
			void WriteBody()
			{
				asio::async_write(m_socket, asio::buffer(m_qMessagesOut.front().body.data(), m_qMessagesOut.front().header.size),
					[this](std::error_code ec, std::size_t length)
					{
						if (!ec)
						{
							m_qMessagesOut.pop_front();

							if (!m_qMessagesOut.empty())
							{
								WriteHeader();
							}
						}
						else
						{
							std::cout << "[" << id << "] Writebody fail.\n";
							m_socket.close();
						}
					}
				);
			}

			void AddToIncomingMessageQueue()
			{
				if (m_nOwnerType == owner::server)
					m_qMessagesIn.push_back({ this->shared_from_this(), m_msgTemporaryIn });
				else
					m_qMessagesIn.push_back({ nullptr, m_msgTemporaryIn });

				// Register context to read another header
				ReadHeader();
			}

			// "Encrypt" data
			uint64_t scramble(uint64_t nInput)
			{
				uint64_t out = nInput ^ 0xDEADBEEFC0DECAFE;
				out = (out & 0xF0F0F0F0F0F0F0) >> 4 | (out & 0x0F0F0F0F0F0F0F) << 4;
				return out ^ 0xC0dEFACE12345678;
			}

			void WriteValidation()
			{
				asio::async_write(m_socket, asio::buffer(&m_nHandshakeOut, sizeof(uint64_t)),
					[this](std::error_code ec, std::size_t length)
					{
						if (!ec)
						{
							// Validation data has been sent
							// Client should wait for a response
							if (m_nOwnerType == owner::client)
								ReadHeader();
						}
						else
						{
							m_socket.close();
						}
					}
				);
			}

			void ReadValidation(asr::net::server_interface<T>* server = nullptr)
			{
				asio::async_read(m_socket, asio::buffer(&m_nHandshakeIn, sizeof(uint64_t)),
					[this, server](std::error_code ec, std::size_t length)
					{
						if (!ec)
						{
							if (m_nOwnerType == owner::server)
							{
								if (m_nHandshakeIn == m_nHandshakeCheck)
								{
									// Client has provided valid scramble, allow it to connect
									std::cout << "Client validated\n";
									server->OnClientValidated(this->shared_from_this());

									// Prime asio to read headers
									ReadHeader();
								}
								else
								{
									std::cout << "Client disconnected (Failed validation)\n";
									m_socket.close();
								}
							}
							else
							{
								// Scramble the data received from server
								m_nHandshakeOut = scramble(m_nHandshakeIn);

								// Write the result
								WriteValidation();
							}
						}
						else
						{
							// Uh oh
							std::cout << "Client disconnected (ReadValidation)\n";
							m_socket.close();
						}
					}
				);
			}

		protected:
			// Each connection has a socket to a remote
			asio::ip::tcp::socket m_socket;

			// Reference to a context that's shared with the whole asio instance
			asio::io_context& m_asioContext;
			
			// Queue of messages to be sent to the remote of the connection
			tsqueue<message<T>> m_qMessagesOut;

			// Queue holds messages received from the remote
			// Reference because the "owner" is expected to provide a queue
			tsqueue<owned_message<T>>& m_qMessagesIn;
			message<T> m_msgTemporaryIn;

			// The owner changes some behaviour of the connection
			owner m_nOwnerType = owner::server;

			// ID of the connection
			uint32_t id = 0;

			// Handshake validation
			uint64_t m_nHandshakeOut = 0;
			uint64_t m_nHandshakeIn = 0;
			uint64_t m_nHandshakeCheck = 0;
		};
	}
}