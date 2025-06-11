#include "Server.hpp"

void Server::cleanup_client( socket_t client_socket, std::string username, bool http_request ) {
	// If there is no username set it to "<unknown>"
    if ( username.empty( ) )
        username = "<unknown>";

    // Always remove from the master fd_set
    FD_CLR( client_socket, &master_set_ );

	// Remove the client from the clients set and socket set
    {
        std::lock_guard lock( clients_mutex_ );

        // If the users socket is not found they have already disconnected so we can exit early
		if ( clients_.contains( client_socket ) == false )
			return;

        clients_.erase( client_socket );
        socket_set_.erase( client_socket );
    }

	// Remove the user from the user map
    {
        std::lock_guard lock( user_mutex_ );
        users_.erase( client_socket );
    }

	// Close the client socket
    CLOSESOCKET( client_socket );

    if ( http_request == false ) {
        // Send a disconnect message to all other clients indicating the user has disconnected
        const std::string disconnect_message = std::format( "[{}] Server: {} has disconnected.", Shared::get_current_time( ), username );
        {
            std::lock_guard lock( clients_mutex_ );
            for ( const socket_t& socket : clients_ ) {
                if ( send( socket, disconnect_message.c_str( ), static_cast< int >( disconnect_message.size( ) ), 0 ) < 0 ) {
                    std::cerr << "Failed to send disconnect message: " << GET_ERROR << std::endl;
                }
            }
        }

        // Print the disconnect message to the console
        std::cout << disconnect_message << std::endl;
    }
}

void Server::handle_client( socket_t client_socket ) {
    std::string username = "";

    // Check if the client is a new user or an existing one
    bool is_new_user = false;
    {
        std::lock_guard<std::mutex> user_lock( user_mutex_ );

        // Find the user in the user map
        auto it = users_.find( client_socket );

		// If the users socket is not found they have already disconnected so we can exit early
        if ( it == users_.end( ) )
			return;

        // If the user does not have a username they are new
        is_new_user = it->second.empty( );

        // If the user is not new set the username from the user map
        if ( !is_new_user )
            username = it->second;
    }

    try {
        int bytes_received = 0;
        char username_buffer[ max_username_length + 1U ] = {};
        char message_buffer[ max_message_length + 1U ] = {};

		// If the client is a new user, we need to receive their username
        if ( is_new_user ) {
			// Wait for the client to send their username
            bytes_received = Shared::receive_data( client_socket, username_buffer );
			// Check if the request was a HTTP request
            if ( bytes_received == HTTP_DETECTED )
                throw std::runtime_error( "HTTP request" );

            // Check if we received any data
            if ( bytes_received <= 0 ) {
                const int err = GET_ERROR;
                // If the error is a non-blocking error or interrupted, just return
                if ( bytes_received < 0 && ( err == WOULD_BLOCK || err == EINTR_ERR ) ) return;
                throw std::runtime_error( bytes_received == 0 ? "Client disconnected before username" : std::format( "Username recv failed: {}", err ) );
            }

			// Ensure the username does not exceed the maximum length
            username.assign( username_buffer, std::min( bytes_received, max_username_length ) );

            // Set the username in the user map ensuring it does not exceed the maximum length
            {
                std::lock_guard<std::mutex> user_lock( user_mutex_ );
                users_.at( client_socket ) = username;
            }

			// Send a welcome message to all other clients indicating the new user has joined
            const std::string welcome_message = std::format( "[{}] {} has joined the chat.", Shared::get_current_time( ), username );
            {
                std::lock_guard<std::mutex> clients_lock( clients_mutex_ );
                for ( const socket_t& socket : clients_ ) {
                    if ( socket != client_socket ) {
						if ( send( socket, welcome_message.c_str( ), static_cast< int >( welcome_message.size( ) ), 0 ) < 0 ) {
							throw std::runtime_error( std::format( "Failed to send welcome message: {}", GET_ERROR ) );
						}
                    }
                }
            }

			// Print the welcome message to the console
            std::cout << welcome_message << std::endl;
            return;
        }

		// Wait for the client to send a message
        bytes_received = Shared::receive_data( client_socket, message_buffer );
        // Check if the request was a HTTP request
        if ( bytes_received == HTTP_DETECTED )
            throw std::runtime_error( "HTTP request" );

        // Check if we received any data
        if ( bytes_received <= 0 ) {
            const int err = GET_ERROR;
            // If the error is a non-blocking error or interrupted, just return
            if ( bytes_received < 0 && ( err == WOULD_BLOCK || err == EINTR_ERR ) ) return;
            throw std::runtime_error( bytes_received == 0 ? "Client disconnected before username" : std::format( "Username recv failed: {}", err ) );
        }

		// Get the user message ensuring it does not exceed the maximum length
        std::string user_message( message_buffer, std::min( bytes_received, max_message_length ) );

		// Check if the message starts with the message flag if not return ignore it
        if ( !user_message.starts_with( message_flag ) )
            return;

		// Remove the message flag from the message
        user_message.erase( 0, message_flag.size( ) );

		// Send the final message to all other clients in the chat
        const std::string final_message = std::format( "[{}] {}: {}", Shared::get_current_time( ), username, user_message );
        {
            std::lock_guard<std::mutex> clients_lock( clients_mutex_ );
            for ( const socket_t& socket : clients_ ) {
                if ( socket != client_socket ) {
					if ( send( socket, final_message.c_str( ), static_cast< int >( final_message.size( ) ), 0 ) < 0 )
						throw std::runtime_error( std::format( "Failed to send message: {}", GET_ERROR ) );
                }
            }
        }

		// Print the message to the console
        std::cout << final_message << std::endl;
    }
    catch ( const std::exception& e ) {
        const std::string msg = e.what( );
        cleanup_client( client_socket, username, msg == "HTTP request" );
#ifdef _DEBUG
        std::cout << std::format( "[{}] Client disconnected: {}", Shared::get_current_time( ), msg ) << std::endl;
#endif
    }
}

void Server::accept_new_client( ) {
	// Accept a new client connection
    socket_t client_socket = accept( server_socket_, nullptr, nullptr );
	// Check if the client socket is valid
    if ( client_socket == INVALID_SOCKET_VAL )
        return;

	// Check if the maximum number of connections has been reached
	{
		std::lock_guard<std::mutex> lock( clients_mutex_ );
		if ( clients_.size( ) >= FD_SETSIZE ) {
			std::cerr << "Too many connections." << std::endl;
			CLOSESOCKET( client_socket );
			return;
		}
	}

	// Set the client socket to non-blocking mode
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket( client_socket, FIONBIO, &mode );
#else
    int flags = fcntl( client_socket, F_GETFL, 0 );
    fcntl( client_socket, F_SETFL, flags | O_NONBLOCK );
#endif

    // Add the new client to the master fd_set and socket tracking set
    {
        std::lock_guard<std::mutex> lock( clients_mutex_ );
        clients_.insert( client_socket );
        socket_set_.insert( client_socket );
        FD_SET( client_socket, &master_set_ );
    }

	// Add the new client socket to the user map with an empty username
    {
		std::lock_guard<std::mutex> lock( user_mutex_ );
        users_.insert( { client_socket, "" } );
    }
}

void Server::run( ) {
	// Indicate what ip and port the server is listening on
    std::cout << "Server listening on " << ip << ":" << port << std::endl;

    while ( true ) {
        fd_set read_set;
        FD_ZERO( &read_set );
        int max_fd = 0;

        {
            std::lock_guard<std::mutex> lock( clients_mutex_ );
            // Copy master_set_ to read_set
            read_set = master_set_;
            // Find max_fd from socket_set_
            for ( socket_t s : socket_set_ ) {
                if ( s > max_fd ) max_fd = s;
            }
        }

		// Set the number of file descriptors to monitor
        int nfds = max_fd + 1;

		// Use select to wait for activity on the sockets
        int ready_count = select( nfds, &read_set, nullptr, nullptr, nullptr );

		// Check if select returned an error
        if ( ready_count == SOCKET_ERROR ) {
            std::cerr << "select() failed: " << GET_ERROR << std::endl;
            break;
        }

		// Vector to hold the ready sockets
        std::vector<socket_t> ready_sockets = {};

		// Reserve space for the ready sockets to avoid multiple allocations
        ready_sockets.reserve( socket_set_.size( ) );
        {
			// Add the ready sockets to the vector
            std::lock_guard<std::mutex> lock( clients_mutex_ );
            for ( socket_t s : socket_set_ ) {
				// Check if the socket is ready for reading if it is then client is ready
                if ( FD_ISSET( s, &read_set ) ) {
                    ready_sockets.push_back( s );
                }
            }
        }

		// Iterate over the ready sockets and handle them
        for ( socket_t s : ready_sockets ) {
			// Check if the socket is the server socket
            if ( s == server_socket_ ) {
				// Accept a new client connection
                accept_new_client( );
            }
            else {
				// Send handle_client task to the thread pool
                Shared::post_task( [ this, s ] {
                    handle_client( s );
                } );
            }
        }
    }
}

int main( ) {
    try {
        // Start multithreading
        Shared::start_mt( );

        // Run the Server
        Server( ).run( );
    }
    catch ( const std::exception& e ) {
        std::cerr << "Exception: " << e.what( ) << std::endl;
        return 1;
    }

    return 0;
}