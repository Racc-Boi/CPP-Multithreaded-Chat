#pragma once
#include "../Shared.hpp"

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <netdb.h>
#endif

constexpr static const char* hostname = "hostname.com";

class Client {
private:
    socket_t client_socket_ = {};
public:
    Client( ) {
#ifdef _WIN32
        // Enable virtual terminal processing for Windows console
		HANDLE std_out = GetStdHandle( STD_OUTPUT_HANDLE );
		DWORD mode = 0;
		GetConsoleMode( std_out, &mode );
		SetConsoleMode( std_out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING );

        // Initialize Winsock
        WSADATA wsaData = {};
        if ( WSAStartup( MAKEWORD( 2, 2 ), &wsaData ) != 0 )
            throw std::runtime_error( "WSAStartup failed" );
#else
		// Enable raw input mode for Unix-like systems
        termios term;
        tcgetattr( STDIN_FILENO, &term );
        term.c_lflag &= ~( ICANON | ECHO );
        tcsetattr( STDIN_FILENO, TCSANOW, &term );
#endif
        // Create a socket
        client_socket_ = socket( AF_INET, SOCK_STREAM, 0 );
		// Check if the socket was created successfully
        if ( client_socket_ == INVALID_SOCKET_VAL ) {
            // Cleanup and throw error
#ifdef _WIN32
            // Cleanup Winsock
            WSACleanup( );
#endif
            throw std::runtime_error( "Could not create socket" );
        }

        // Resolve hostname to IPv4 address
        addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        // Use getaddrinfo to resolve the hostname
        addrinfo* result = nullptr;
        if ( getaddrinfo( hostname, nullptr, &hints, &result ) != 0 || result == nullptr ) {
            // Close the socket and clean up
            CLOSESOCKET( client_socket_ );
#ifdef _WIN32
            // Cleanup Winsock
            WSACleanup( );
#endif
            // Free the address info
            freeaddrinfo( result );
            throw std::runtime_error( "Failed to resolve hostname" );
        }

        // Extract the first IPv4 address from the result
        sockaddr_in* sockaddr_ipv4 = reinterpret_cast< sockaddr_in* >( result->ai_addr );
        sockaddr_in server_addr = {};
        // Copy the resolved address and port
        server_addr.sin_addr = sockaddr_ipv4->sin_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons( port );

        // Free the address info
        freeaddrinfo( result );

        // Connect to the server
        while ( connect( client_socket_, reinterpret_cast< struct sockaddr* >( &server_addr ), sizeof( server_addr ) ) < 0 ) {
            // Handle connection failure
            std::cerr << "Connection failed. Retrying in 3 seconds..." << std::endl;
            std::this_thread::sleep_for( std::chrono::seconds( 3 ) );

            // Check for connection attempts limit and clean up if exceeded
            static int attempts = 0;
            if ( ++attempts >= 5 ) {
                // Close the socket and clean up
                CLOSESOCKET( client_socket_ );
#ifdef _WIN32
                // Cleanup Winsock
                WSACleanup( );
#endif
                // Free the address info
                freeaddrinfo( result );
                throw std::runtime_error( "Could not connect to server" );
            }
        }
    }

    ~Client( ) {
        // Notify all threads to shut down
        Shared::end_mt( );
        // Shutdown the client socket
        shutdown( client_socket_, SD_BOTH );
        // Close the client socket
        CLOSESOCKET( client_socket_ );
#ifdef _WIN32
        // Cleanup Winsock
        WSACleanup( );
#endif
    }
private:
    void read_messages( );
    void send_messages( );
public:
    void run( const std::string& username );
};
