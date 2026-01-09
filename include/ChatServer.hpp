/**
 * @file ChatServer.hpp
 * @brief C++ wrapper for TCP Chat Server
 * 
 * Provides a modern C++ interface to the C-based chat server
 */

#ifndef CHAT_SERVER_HPP
#define CHAT_SERVER_HPP

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <stdexcept>

extern "C" {
    #include "common.h"
    #include "protocol.h"
}

namespace chat {

/**
 * @brief Exception thrown by ChatServer operations
 */
class ServerException : public std::runtime_error {
public:
    explicit ServerException(const std::string& msg) 
        : std::runtime_error(msg) {}
};

/**
 * @brief Represents a connected client
 */
struct ClientInfo {
    std::string username;
    std::string ip_address;
    uint16_t port;
    int socket_fd;
    bool is_authenticated;

    ClientInfo() : port(0), socket_fd(-1), is_authenticated(false) {}
};

/**
 * @brief Modern C++ interface for the chat server
 * 
 * Provides RAII, exception safety, and callback-based event handling
 */
class ChatServer {
public:
    using MessageCallback = std::function<void(const ClientInfo&, const std::string&)>;
    using ConnectionCallback = std::function<void(const ClientInfo&)>;

    /**
     * @brief Construct a new Chat Server
     * @param port Port to listen on
     * @param max_clients Maximum number of concurrent clients
     * @throws ServerException if initialization fails
     */
    ChatServer(uint16_t port, int max_clients);

    /**
     * @brief Destructor ensures cleanup
     */
    ~ChatServer();

    // Disable copy (socket ownership)
    ChatServer(const ChatServer&) = delete;
    ChatServer& operator=(const ChatServer&) = delete;

    // Enable move
    ChatServer(ChatServer&&) noexcept;
    ChatServer& operator=(ChatServer&&) noexcept;

    /**
     * @brief Start the server (blocking)
     * @throws ServerException on error
     */
    void run();

    /**
     * @brief Stop the server gracefully
     */
    void stop();

    /**
     * @brief Set callback for new messages
     */
    void onMessage(MessageCallback callback) {
        message_callback_ = std::move(callback);
    }

    /**
     * @brief Set callback for new connections
     */
    void onConnect(ConnectionCallback callback) {
        connect_callback_ = std::move(callback);
    }

    /**
     * @brief Set callback for disconnections
     */
    void onDisconnect(ConnectionCallback callback) {
        disconnect_callback_ = std::move(callback);
    }

    /**
     * @brief Get list of connected clients
     */
    std::vector<ClientInfo> getClients() const;

    /**
     * @brief Broadcast message to all clients
     */
    void broadcast(const std::string& message);

    /**
     * @brief Send message to specific client
     */
    void sendToClient(const std::string& username, const std::string& message);

    /**
     * @brief Get server statistics
     */
    struct Stats {
        int current_clients;
        int total_connections;
        uint64_t messages_sent;
        uint64_t bytes_transferred;
    };

    Stats getStats() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;

    MessageCallback message_callback_;
    ConnectionCallback connect_callback_;
    ConnectionCallback disconnect_callback_;
};

/**
 * @brief RAII wrapper for client connection
 */
class ChatClient {
public:
    /**
     * @brief Connect to a chat server
     * @param server_ip Server IP address
     * @param port Server port
     * @param username Username for this client
     * @throws ServerException if connection fails
     */
    ChatClient(const std::string& server_ip, uint16_t port, 
               const std::string& username);

    ~ChatClient();

    // Disable copy
    ChatClient(const ChatClient&) = delete;
    ChatClient& operator=(const ChatClient&) = delete;

    /**
     * @brief Send a message to the chat
     */
    void send(const std::string& message);

    /**
     * @brief Receive messages (blocking call)
     * @param callback Function called for each message
     */
    void receive(std::function<void(const std::string&)> callback);

    /**
     * @brief Disconnect from server
     */
    void disconnect();

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace chat

#endif // CHAT_SERVER_HPP
