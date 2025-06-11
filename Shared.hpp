#include <iostream>
#include <thread>
#include <string>
#include <chrono>
#include <format>
#include <memory>
#include <mutex>
#include <functional>
#include <queue>
#include <condition_variable>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using socket_t = SOCKET;
#define CLOSESOCKET closesocket
#define GET_ERROR WSAGetLastError()
#define INVALID_SOCKET_VAL INVALID_SOCKET
#define WOULD_BLOCK WSAEWOULDBLOCK
#define EINTR_ERR WSAEINTR
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
using socket_t = int;
#define CLOSESOCKET close
#define GET_ERROR errno
#define INVALID_SOCKET_VAL -1
#define ioctlsocket(fd, cmd, argp) ioctl(fd, cmd, argp)
#define SD_BOTH SHUT_RDWR
#define WOULD_BLOCK EWOULDBLOCK
#define EINTR_ERR EINTR
#define SOCKET_ERROR -1
#endif

constexpr static const int port = 12345;
constexpr static const int max_username_length = 32;
constexpr static const int max_message_length = 1036;
constexpr static const std::string_view message_flag = "[ MESSAGE ] ";
constexpr static const int HTTP_DETECTED = std::numeric_limits<int>::min( );
static const unsigned int max_threads = std::max( 1u, std::thread::hardware_concurrency( ) );

namespace Shared {
    inline std::vector<std::jthread> thread_pool_;
    inline std::queue<std::function<void( )>> task_queue_;
    inline std::condition_variable task_cv_;
    inline std::mutex task_mutex_;
    inline bool shutting_down_ = false;

	// Helper function to get the current time as a string
    inline std::string get_current_time( ) {
		// Get the current time and format it
		auto now = std::chrono::system_clock::now( );
		// Convert to time_t
		auto now_time = std::chrono::system_clock::to_time_t( now );
		// Convert to local time
		std::ostringstream ss;
        // Format the time as YYYY-MM-DD HH:MM:SS
        std::tm local_tm;
#ifdef _WIN32
        localtime_s( &local_tm, &now_time );
#else
        localtime_r( &now_time, &local_tm );
#endif
        ss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
		// Return the formatted time as a string
		return ss.str( );
	}

    inline void worker_thread( ) {
        while ( true ) {
            std::function<void( )> task;
            {
                std::unique_lock<std::mutex> lock( task_mutex_ );
                task_cv_.wait( lock, [ & ] { return shutting_down_ || !task_queue_.empty( ); } );
                if ( shutting_down_ && task_queue_.empty( ) ) return;
                task = std::move( task_queue_.front( ) );
                task_queue_.pop( );
            }
            task( );
        }
    }

    inline void post_task( std::function<void( )> task ) {
        {
            std::lock_guard<std::mutex> lock( task_mutex_ );
            task_queue_.emplace( std::move( task ) );
        }
        task_cv_.notify_one( );
    }

    inline void start_mt( ) {
        // Create thread pool
        for ( unsigned int i = 0; i < max_threads; ++i ) {
            thread_pool_.emplace_back( &worker_thread );
        }
    }

    inline void end_mt( ) {
        {
            std::lock_guard<std::mutex> lock( task_mutex_ );
            shutting_down_ = true;
            task_cv_.notify_all( );
        }
        for ( auto& t : thread_pool_ ) {
            if ( t.joinable( ) ) {
                t.join( );
            }
        }
        thread_pool_.clear( );
    }

    template <std::size_t N>
    inline int receive_data( socket_t socket, char( &buffer )[ N ] ) {
        // Ensure the buffer is large enough to hold data and a null terminator
        static_assert( N > 1, "Buffer must be at least 2 bytes to allow space for null terminator" );

		// Peek at the incoming data to check for HTTP requests or TLS ClientHello
        char peek_buf[ 8 ] = {};
        if ( const int peeked = recv( socket, peek_buf, sizeof( peek_buf ) - 1, MSG_PEEK );
             peeked > 0 ) {
            peek_buf[ peeked ] = '\0';  // ensure null-terminated for strstr/strncmp safety

            // Check for TLS ClientHello (first byte 0x16 indicates a handshake record)
            if ( static_cast< unsigned char >( peek_buf[ 0 ] ) == 0x16 )
                return HTTP_DETECTED;

            // Detect common HTTP methods or headers
            constexpr static const std::string_view http_methods[ ] = { "GET", "POST", "HEAD", "PUT", "DELETE" };
            for ( const auto& method : http_methods ) {
				// Check if the peeked buffer is a known HTTP method
                if ( strncmp( peek_buf, method.data( ), method.size( ) ) == 0 )
                    return HTTP_DETECTED;
            }

			// Check for HTTP version in the peeked buffer
            if ( strstr( peek_buf, "HTTP/" ) != nullptr )
                return HTTP_DETECTED;
        }

        // Clear the buffer before receiving data
        const int bytes_received = recv( socket, buffer, static_cast< int >( N - 1 ), 0 );
        if ( bytes_received <= 0 )
            return bytes_received;

        // Null-terminate the received data
        buffer[ bytes_received ] = '\0';

        // Discard any leftover data beyond buffer
        if ( bytes_received == static_cast< int >( N - 1 ) ) {
            char discard_buffer[ 128 ];
            while ( true ) {
				// Attempt to read more data to discard
                const int extra_bytes = recv( socket, discard_buffer, sizeof( discard_buffer ), 0 );
				// If no more data is available, break the loop
                if ( extra_bytes <= 0 )
                    break;
            }
        }

        return bytes_received;
    }
}