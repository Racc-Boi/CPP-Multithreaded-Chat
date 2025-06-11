#include "Client.hpp"

// Message prompt
constexpr const static std::string_view enter_message = "Enter message: ";

// User input buffer to hold the current message being typed
std::string user_input_buffer = "";

void read_user_input( std::string& buffer ) {
    buffer.clear( );
    while ( true ) {
        char c;
#ifdef _WIN32
        c = _getch( );
#else
        read( STDIN_FILENO, &c, 1 );
#endif
        if ( c == '\n' || c == '\r' ) break;

        if ( c == 127 || c == '\b' ) { // backspace
            if ( !buffer.empty( ) ) {
                buffer.pop_back( );
                std::cout << "\b \b" << std::flush;
            }
        }
        else {
            buffer.push_back( c );
            std::cout << c << std::flush;
        }
    }
    std::cout << "\n";
}

void Client::read_messages( ) {
    try {
        while ( true ) {
			// Wait for a message from the server
			char message_buffer[ max_message_length + 1U ] = {};
            const int bytes_received = Shared::receive_data( client_socket_, message_buffer );
			// Check if the request was a HTTP request
            if ( bytes_received == HTTP_DETECTED )
                throw std::runtime_error( "HTTP request, ignoring" );

			// Check if we received any data
            if ( bytes_received <= 0 ) {
                const int err = GET_ERROR;
				// If the error is a non-blocking error or interrupted, just return
                if ( bytes_received < 0 && ( err == WOULD_BLOCK || err == EINTR_ERR ) ) return;
				// Throw an error indicating the server disconnected or a recv failure
				throw std::runtime_error( bytes_received == 0 ? "Server disconnected" : std::format( "Message recv failed: {}", err ) );
            }

			// Clear the enter message prompt line and print the received message
            std::cout << "\33[2K\r" << message_buffer << "\n";

            // Reprint input
            std::cout << enter_message << user_input_buffer << std::flush;
        }
    }
    catch ( const std::exception& e ) {
        std::cerr << std::format( "Exception: {}", e.what( ) ) << std::endl;
    }
}

void Client::send_messages( ) {
    try {
        while ( true ) {
			// Prompt the user for a message
            std::cout << enter_message << std::flush;

			// Read user input into the buffer
			read_user_input( user_input_buffer );

			// Check if the user input is empty if so continue to the next iteration
            if ( user_input_buffer.empty( ) ) {
				// Remove previous prompt line
                std::cout << "\33[A\33[2K\r";
                continue;
            }

            // Add message flag to the message
            user_input_buffer.insert( 0, message_flag.data( ) );

			// Ensure the message does not exceed the maximum length
            const std::string safe_msg( user_input_buffer.c_str( ), std::min( static_cast<int>( user_input_buffer.size( ) ), max_message_length ) );

			// Send the message to the server
			if ( send( client_socket_, safe_msg.c_str( ), static_cast< int >( safe_msg.size( ) ), 0 ) < 0 ) {
				throw std::runtime_error( std::format( "Failed to send message: {}", GET_ERROR ) );
			}

            // Clear prompt line
            std::cout << "\33[A\33[2K\r";

            // Remove message flag from the message
            user_input_buffer.erase( 0, message_flag.size( ) );

			// Format and print the message with timestamp
            const std::string formatted = std::format( "[{}] You: {}", Shared::get_current_time( ), user_input_buffer );
            std::cout << formatted << std::endl;

			// Clear the input buffer for the next message
            user_input_buffer.clear( );
        }
    }
    catch ( const std::exception& e ) {
        std::cerr << std::format( "Exception: {}", e.what( ) ) << std::endl;
    }
}

void Client::run( const std::string& username ) {
	// Print the username to the console
    std::cout << "Logged in as: " << username << std::endl;

    // Send the username to the server
	if ( send( client_socket_, username.c_str( ), static_cast< int >( username.size( ) ), 0 ) < 0 ) {
		throw std::runtime_error( std::format( "Failed to send username: {}", GET_ERROR ) );
	}

	// Start reading and sending messages sending the tasks to the thread pool
    Shared::post_task( [ this ] { read_messages( ); } );
    Shared::post_task( [ this ] { send_messages( ); } );
}

int main( ) {
    try {
		std::string username = "";
        do {
            // Prompt the user for a username
            std::cout << "Enter your username: " << std::flush;

            // Read user input into the buffer
            read_user_input( user_input_buffer );

            // Check if the user input is empty if so continue to the next iteration
            if ( user_input_buffer.empty( ) ) {
                // Remove previous prompt line
                std::cout << "\33[A\33[2K\r";
                continue;
            }

            // set username to the user input buffer
            username = std::string( user_input_buffer.c_str( ), std::min( static_cast< int >( user_input_buffer.size( ) ), max_username_length ) );

            // Clear the input buffer for the next message
            user_input_buffer.clear( );
        } while ( username.empty( ) );

        // Start multithreading
        Shared::start_mt( );

        // Run the client
        Client( ).run( username );
    }
    catch ( const std::exception& e ) {
        std::cerr << std::format( "Exception: {}", e.what( ) ) << std::endl;
        return 1;
    }

    return 0;
}
