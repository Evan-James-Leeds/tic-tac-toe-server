// NOTE: must use option -pthread when compiling!
#define _POSIX_C_SOURCE 200809L
#define BUFSIZE 256
#define HOSTSIZE 100
#define PORTSIZE 10
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>


#define QUEUE_SIZE 8

typedef enum
{
    PLAY,
    SUGDRAW,
    ACCDRAW,
    REJDRAW,
    RESIGN,
    MOVE,
    INVALID,
    BAD_COMMAND
} command_type;

pthread_mutex_t connecting_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t connecting_cond = PTHREAD_COND_INITIALIZER;

struct connection_data *waiting_client;

/*
LL of unique usernames
*/
typedef struct Node
{
    char *name;
    struct Node *next;
} Node;

pthread_mutex_t names_mutex = PTHREAD_MUTEX_INITIALIZER;
Node *unique_names = NULL;

/*
Stores data about a player's input to the server
*/
typedef struct player_input
{
    command_type type;
    char name[128];
    char x_or_o;                   // X , 0
    char vertical_pos;             //  1 , 2 , 3
    char horizontal_pos;           // 1, 2 , 3
    char client_response_msg[100]; //
} player_input;

typedef struct client_pair_t client_pair_t;

/*
Stores data about a single connected client
*/
struct connection_data
{
    struct sockaddr_storage addr; // Player's IP + Port
    socklen_t addr_len;           // Length of address
    int fd;                       // File Descriptor for players socket
    char name[128];               // Player's name
    char role;                    // Player's role
    int index;
    char wants_draw;
    client_pair_t *pair; // Reference to the client pair
    pthread_t thread_id;
};

/*
LL of clients attempting to connect
*/
typedef struct ClientList
{
    struct connection_data *data;
    struct ClientList *next;
} ClientList;

pthread_mutex_t connected_clients_mutex = PTHREAD_MUTEX_INITIALIZER;
int numConnecting = 0;
ClientList *connecting_clients = NULL;

/*
Stores data about a pair of clients connected to each other
*/
typedef struct client_pair_t
{
    struct connection_data *clients[2]; // Two players
    char board[10];                     // Board data
    char currentTurn;                   // Current turn
    int moves;
    int gameOver;
} client_pair_t;

volatile int active = 1;

/*
If signal is receieved that is bound to handler.
Causes the system to report shutdown and close resources.
*/
void handler(int signum)
{
    active = 0;
}

/*
Sets handlers for interrupt and terminate signals for primary thread.
*/
void install_handlers(sigset_t *mask)
{
    struct sigaction act;
    act.sa_handler = handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    sigemptyset(mask);
    sigaddset(mask, SIGINT);
    sigaddset(mask, SIGTERM);
}

void thread_signal_handler(int sig)
{
    if (sig == SIGUSR1)
    {
        pthread_exit(NULL);
    }
}

/*
Adds a client that is attempting to connect
*/
void add_client(struct connection_data *data)
{
    pthread_mutex_lock(&connected_clients_mutex);
    ClientList *newNode = (ClientList *)malloc(sizeof(ClientList));
    newNode->data = data;
    newNode->next = connecting_clients;
    connecting_clients = newNode;
    numConnecting++;
    pthread_mutex_unlock(&connected_clients_mutex);
}

void remove_client(struct connection_data *data)
{
    pthread_mutex_lock(&connected_clients_mutex);
    ClientList *current = connecting_clients;
    ClientList *prev = NULL;
    while (current != NULL)
    {
        if (current->data->fd == data->fd)
        {
            if (prev != NULL)
            {
                prev->next = current->next;
            }
            else
            {
                connecting_clients = current->next;
            }
            numConnecting--;
            free(current);
            break;
        }

        prev = current;
        current = current->next;
    }
    pthread_mutex_unlock(&connected_clients_mutex);
}
/*
Adds username to LL if not already taken.
*/
int add_username(const char *name)
{
    pthread_mutex_lock(&names_mutex);

    Node *current = unique_names;
    while (current != NULL)
    {
        if (strcmp(current->name, name) == 0)
        {
            pthread_mutex_unlock(&names_mutex);
            return EXIT_FAILURE;
        }
        current = current->next;
    }

    Node *newNode = (Node *)malloc(sizeof(Node));
    newNode->name = strdup(name);
    newNode->next = unique_names;
    unique_names = newNode;

    pthread_mutex_unlock(&names_mutex);
    return EXIT_SUCCESS;
}

/*
Removes a username from the list.
*/
void remove_username(const char *name)
{
    pthread_mutex_lock(&names_mutex);

    Node *current = unique_names;
    Node *prev = NULL;
    while (current != NULL)
    {
        if (strcmp(current->name, name) == 0)
        {
            if (prev != NULL)
            {
                prev->next = current->next;
            }
            else
            {
                unique_names = current->next;
            }

            free(current->name);
            free(current);
            break;
        }

        prev = current;
        current = current->next;
    }
    pthread_mutex_unlock(&names_mutex);
}

/*
Frees all memory space allocated for storing usernames
*/
void free_unique_names()
{
    pthread_mutex_unlock(&names_mutex);

    Node *current = unique_names;
    while (current != NULL)
    {
        Node *next = current->next;
        free(current->name);
        free(current);
        current = next;
    }
    pthread_mutex_unlock(&names_mutex);
}

/*
Initializes a new game's state.
*/
void initializeNewGame(client_pair_t *gameInstance)
{
    // Initialize the board
    memcpy(gameInstance->board, ".........\0", 10);
    gameInstance->currentTurn = 'X';

    // Assign roles
    gameInstance->clients[0]->role = 'X';
    gameInstance->clients[1]->role = 'O';

    gameInstance->moves = 0;
}

/*
Used to open a port for the server to listen from.
*/
int open_listener(char *service, int queue_size)
{
    struct addrinfo hint, *info_list, *info;
    int error, sock;
    // initialize hints
    memset(&hint, 0, sizeof(struct addrinfo));
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_PASSIVE;
    // obtain information for listening socket
    error = getaddrinfo(NULL, service, &hint, &info_list);
    if (error)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        return -1;
    }
    // attempt to create socket
    for (info = info_list; info != NULL; info = info->ai_next)
    {
        sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        // if we could not create the socket, try the next method
        if (sock == -1)
            continue;
        // bind socket to requested port
        error = bind(sock, info->ai_addr, info->ai_addrlen);
        if (error)
        {
            close(sock);
            continue;
        }
        // enable listening for incoming connection requests
        error = listen(sock, queue_size);
        if (error)
        {
            close(sock);
            continue;
        }
        // if we got this far, we have opened the socket
        break;
    }
    freeaddrinfo(info_list);
    // info will be NULL if no method succeeded
    if (info == NULL)
    {
        fprintf(stderr, "Could not bind\n");
        return -1;
    }
    return sock;
}

player_input error_bad_command(char *error_msg)
{
    player_input ret;
    int error_msg_len = strlen(error_msg) + 23;
    
    memset(&ret, 0, sizeof(player_input));
    strcpy((char *)ret.client_response_msg, "OVER|");
    sprintf((char*)(ret.client_response_msg + 5), "%d", error_msg_len);
    strcat((char *)ret.client_response_msg, "|L|You gave bad input. ");
    strcat((char *)ret.client_response_msg, error_msg);
    strcat((char *)ret.client_response_msg, "|\n");
    ret.type = BAD_COMMAND;
    return ret;
}

int count_delimiters(char *str)
{
    int count = 0;

    for (int i = 0; str[i]; i++)
    {
        if (str[i] == '|')
            count++;
    }

    return count;
}

player_input parse(char *unparsed_input)
{
    if (unparsed_input == NULL)
    {
        return error_bad_command("Error, null input string.");
    }
    char *state;
    char input[BUFSIZE];
    strcpy(input, unparsed_input);

    player_input ret;
    memset(&ret, 0, sizeof(player_input));

    /*
    Parse protocol type & number of bytes
    */
    char *protocol = strtok_r(input, "|", &state);
    if (protocol == NULL)
    {
        return error_bad_command("Error, protocol is null.");
    }
    char *bytes = strtok_r(NULL, "|", &state);
    if (bytes == NULL)
    {
        return error_bad_command("Error, bad format"); // Missing | after length
    }
    int first_msg_char = strlen(bytes) + strlen(protocol) + 2;
    int msg_len = strlen(input + first_msg_char);

    if (atoi(bytes) != msg_len || msg_len > 256)
    {
        return error_bad_command("Error, bad length");
    }

    char *field_1;
    char *field_2;
    if (strcmp(protocol, "WAIT") == 0 || strcmp(protocol, "BEGN") == 0 || strcmp(protocol, "MOVD") == 0 || strcmp(protocol, "INVL") == 0 || strcmp(protocol, "OVER") == 0)
    {
        ret.type = INVALID;
        strcpy(ret.client_response_msg, "INVL|39|User command contains server protocol.|\n");
        return ret;
    }
    else if (strcmp(protocol, "PLAY") == 0)
    {
        if (count_delimiters(unparsed_input) != 3) // Expecting 3 '|' characters for PLAY
            return error_bad_command("Error, incorrect number of fields for PLAY.");

        field_1 = strtok_r(NULL, "|", &state);
        ret.type = PLAY;

        // Conditionals to verify format of the PLAY input
        if (field_1 == NULL)
            return error_bad_command("Error, no name given");
        else
        {
            field_2 = strtok_r(NULL, "|", &state);
            if (field_2 != NULL)
            {
                return error_bad_command("Error, unexpected data past the last delimiter.");
            }
        }
        strcpy((char *)&ret.name, field_1);
    }
    else if (strcmp(protocol, "DRAW") == 0)
    {
        if (count_delimiters(unparsed_input) != 3) // Expecting 3 '|' characters for DRAW
            return error_bad_command("Error, incorrect number of fields for DRAW.");

        field_1 = strtok_r(NULL, "|", &state);

        // Conditionals to verify format of the DRAW input
        if (field_1 == NULL)
            return error_bad_command("Error, no message in requested draw.");
        else
        {
            field_2 = strtok_r(NULL, "|", &state);
            if (field_2 != NULL)
                return error_bad_command("Error, unexpected data past the last delimiter.");
        }

        switch (field_1[0])
        {
        case 'R':
            ret.type = REJDRAW;
            break;
        case 'S':
            ret.type = SUGDRAW;
            break;
        case 'A':
            ret.type = ACCDRAW;
            break;
        default:
            ret.type = INVALID;
            strcpy((char *)&ret.client_response_msg,
                   "INVL|44|S to suggest draw, A to accept, R to reject|\n");
        }
    }
    else if (strcmp(protocol, "RSGN") == 0)
    {
        if (count_delimiters(unparsed_input) != 2) // Expecting 2 '|' characters for RSGN
            return error_bad_command("Error, incorrect number of fields for RSGN.");

        field_1 = strtok_r(NULL, "|", &state);
        if (field_1 != NULL)
            return error_bad_command("Error, unexpected data past the last delimiter.");

        ret.type = RESIGN; // the command RSGN is always correct at this point
    }
    else if (strcmp(protocol, "MOVE") == 0)
    {
        if (count_delimiters(unparsed_input) != 4) // Expecting 4 '|' characters for MOVE
            return error_bad_command("Error, incorrect number of fields for MOVE.");

        field_1 = strtok_r(NULL, "|", &state);
        field_2 = strtok_r(NULL, "|", &state);
        ret.type = MOVE;
        if (field_1 == NULL || field_2 == NULL)
            return error_bad_command("Error, incomplete message.");
        if (field_1[0] == 'X')
            ret.x_or_o = 'X';
        else if (field_1[0] == 'O')
            ret.x_or_o = 'O';
        else
            return error_bad_command("Error, selected role other than X or O.");

        if ((field_2[0] < '1' || field_2[0] > '3') || (field_2[2] < '1' || field_2[2] > '3'))
        {
            strcpy((char *)&ret.client_response_msg,
                   "INVL|55|Position must be in the form x,y with {1,2,3} for each|\n");
            ret.type = INVALID;
        }

        char *field_3 = strtok_r(NULL, "|", &state);
        if (field_3 != NULL)
            return error_bad_command("Error, unexpected data past the last delimiter.");

        ret.horizontal_pos = field_2[0];
        ret.vertical_pos = field_2[2];
    }
    else
        return error_bad_command("Error, command not recognized.");

    return ret;
}

void movd(client_pair_t *gameInstance, int x, int y)
{
    char board_message[BUFSIZE];
    snprintf(board_message, BUFSIZE, "MOVD|16|%c|%d,%d|%s|\n", gameInstance->currentTurn, x, y, gameInstance->board);

    write(gameInstance->clients[0]->fd, board_message, strlen(board_message));
    write(gameInstance->clients[1]->fd, board_message, strlen(board_message));
}

void send_termination_message(int fd)
{
    char const *termination = "OVER|48|W|The other user has terminated the connection.|\n";
    write(fd, termination, strlen(termination));
}

char checkWinner(client_pair_t *gameInstance)
{
    char b[3][3];
    int k = 0;
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            b[i][j] = gameInstance->board[k++];
        }
    }

    for (int i = 0; i < 3; i++)
    {
        // Check rows
        if (b[i][0] == b[i][1] && b[i][1] == b[i][2] && b[i][0] != '.')
            return b[i][0];

        // Check columns
        if (b[0][i] == b[1][i] && b[1][i] == b[2][i] && b[0][i] != '.')
            return b[0][i];
    }

    // Check diagonals
    if (b[0][0] == b[1][1] && b[1][1] == b[2][2] && b[0][0] != '.')
        return b[0][0];
    if (b[0][2] == b[1][1] && b[1][1] == b[2][0] && b[0][2] != '.')
        return b[0][2];

    // No winner found
    return '.';
}

int playerMove(client_pair_t *gameInstance, int x, int y)
{
    int location = x + ((y - 1) * 3) - 1;
    if (gameInstance->board[location] != '.') // Spot has already been taken
        return EXIT_FAILURE;
    else
    {
        gameInstance->board[location] = gameInstance->currentTurn;
        movd(gameInstance, x, y);
        if (gameInstance->currentTurn == 'X')
            gameInstance->currentTurn = 'O';
        else
            gameInstance->currentTurn = 'X';

        return EXIT_SUCCESS;
    }
}

void remove_newline(char *str)
{
    int len = strlen(str);
    if (len > 0 && str[len - 1] == '\n')
    {
        str[len - 1] = '\0';
    }
}

void cleanup_and_close(client_pair_t *con, int player_index, int other_player_index)
{
    // Close client connections
    close(con->clients[player_index]->fd);
    close(con->clients[other_player_index]->fd);

    // Remove usernames
    remove_username(con->clients[player_index]->name);
    remove_username(con->clients[other_player_index]->name);
    
    // Free client resources
    free(con->clients[player_index]);

    con->gameOver = 1;

    // Kill other thread
    pthread_kill(con->clients[other_player_index]->thread_id, SIGUSR1);
}

int process_player_move(client_pair_t *con, int player_index)
{
    char buf[BUFSIZE];
    player_input parsedInputs;

    do
    {
        int bytes_read = 0;
        int total_bytes = 0;
        strcpy(buf,"");
        // Continue reading until the desired number of bytes is received
        while (total_bytes < BUFSIZE - 1)
        {
            bytes_read = read(con->clients[player_index]->fd, buf + total_bytes, BUFSIZE - total_bytes - 1);
            if(con->gameOver == 1){
                return 2;
            }
            if (bytes_read < 0)
            {
                // Error reading from the socket
                send_termination_message(con->clients[1 - player_index]->fd);
                cleanup_and_close(con, player_index, 1 - player_index);
                return 1;
            }
            if (bytes_read == 0)
            {
                // Connection closed by the other end
                send_termination_message(con->clients[1 - player_index]->fd);
                cleanup_and_close(con, player_index, 1 - player_index);
                return 1;
            }

            total_bytes += bytes_read;
            if (buf[total_bytes - 1] == '\n')
            {
                // End of message reached
                break;
            }
        }
        buf[total_bytes] = '\0';
        remove_newline(buf);
        parsedInputs = parse(buf);
        if(parsedInputs.type == INVALID){
            snprintf(buf, BUFSIZE, "%s", parsedInputs.client_response_msg);
            write(con->clients[player_index]->fd, buf, strlen(buf));
        }
        else if (con->clients[player_index]->wants_draw && parsedInputs.type != RESIGN)
        {
            parsedInputs.type = INVALID;
            snprintf(buf, BUFSIZE, "INVL|48|Waiting for opponent's reponse to draw request.|\n");
            write(con->clients[player_index]->fd, buf, strlen(buf));
        }
        else if (parsedInputs.type == PLAY)
        {
            parsedInputs.type = INVALID;
            snprintf(buf, BUFSIZE, "INVL|42|You cannot start a new game at this time.|\n");
            write(con->clients[player_index]->fd, buf, strlen(buf));
        }
        else if (parsedInputs.type == MOVE)
        {
            if (con->clients[1 - player_index]->wants_draw)
            {
                parsedInputs.type = INVALID;
                snprintf(buf, BUFSIZE, "INVL|43|Draw request must be rejected or accepted.|\n");
                write(con->clients[player_index]->fd, buf, strlen(buf));
            }
            else if (con->clients[player_index]->role != con->currentTurn)
            {
                parsedInputs.type = INVALID;
                snprintf(buf, BUFSIZE, "INVL|21|It is not your turn.|\n");
                write(con->clients[player_index]->fd, buf, strlen(buf));
            }
            else if (parsedInputs.x_or_o != con->clients[player_index]->role)
            {
                parsedInputs.type = INVALID;
                snprintf(buf, BUFSIZE, "INVL|25|Incorrect role selected.|\n");
                write(con->clients[player_index]->fd, buf, strlen(buf));
            }
            else if (playerMove(con, parsedInputs.horizontal_pos - '0', parsedInputs.vertical_pos - '0'))
            {
                parsedInputs.type = INVALID;
                snprintf(buf, BUFSIZE, "INVL|24|That space is occupied.|\n");
                write(con->clients[player_index]->fd, buf, strlen(buf));
            }
        }
        else if (parsedInputs.type == REJDRAW)
        {
            if (con->clients[1 - player_index]->wants_draw)
            {
                buf[strlen(buf)] = '\n';
                write(con->clients[1 - player_index]->fd, buf, strlen(buf));
                con->clients[1 - player_index]->wants_draw = 0;
            }
            else
            {
                snprintf(buf, BUFSIZE, "INVL|23|No draw was requested.|\n");
                write(con->clients[player_index]->fd, buf, strlen(buf));
            }
        }
        else if (parsedInputs.type == ACCDRAW)
        {
            if (con->clients[1 - player_index]->wants_draw)
            {
                snprintf(buf, BUFSIZE, "OVER|26|D|Players agreed to draw.|\n");
                write(con->clients[1]->fd, buf, strlen(buf));
                write(con->clients[0]->fd, buf, strlen(buf));
                cleanup_and_close(con, player_index, 1 - player_index);
                return 1;
            }
            else
            {
                snprintf(buf, BUFSIZE, "INVL|23|No draw was requested.|\n"); 
                write(con->clients[player_index]->fd, buf, strlen(buf));
            }
        }
        else if (parsedInputs.type == SUGDRAW)
        {
            if (con->clients[1 - player_index]->wants_draw)
            {
                parsedInputs.type = INVALID;
                snprintf(buf, BUFSIZE, "INVL|43|Draw request must be rejected or accepted.|\n");
                write(con->clients[player_index]->fd, buf, strlen(buf));
            }
            else
            {
                con->clients[player_index]->wants_draw = 1;
                buf[strlen(buf)] = '\n';
                write(con->clients[1 - player_index]->fd, buf, strlen(buf));
            }
        }
        else if (parsedInputs.type == RESIGN)
        {
            int msgSize = strlen(con->clients[player_index]->name) + 17;
            snprintf(buf, BUFSIZE, "OVER|%d|W|%s has resigned.|\n", msgSize, con->clients[player_index]->name);
            write(con->clients[1 - player_index]->fd, buf, strlen(buf));
            snprintf(buf, BUFSIZE, "OVER|21|L|You have resigned.|\n");
            write(con->clients[player_index]->fd, parsedInputs.client_response_msg, strlen(parsedInputs.client_response_msg));
            cleanup_and_close(con, player_index, 1 - player_index);
            return 1;
        }
        else if (parsedInputs.type == BAD_COMMAND)
        {
            int msgSize = strlen(con->clients[player_index]->name) + 17;
            snprintf(buf, BUFSIZE, "OVER|%d|W|%s disconnected.|\n", msgSize, con->clients[player_index]->name);
            write(con->clients[1 - player_index]->fd, buf, strlen(buf));
            snprintf(buf, BUFSIZE, "INVL|44|Error reading data, terminating connection.|\n");
            write(con->clients[player_index]->fd, buf, strlen(buf));
            cleanup_and_close(con, player_index, 1 - player_index);
            return 1;
        }

    } while (parsedInputs.type == INVALID);

    return 0;
}

client_pair_t *create_game()
{
    client_pair_t *client_pair = (client_pair_t *)malloc(sizeof(client_pair_t));

    // Get the first two clients
    client_pair->clients[1] = connecting_clients->data;
    remove_client(connecting_clients->data);

    client_pair->clients[0] = connecting_clients->data;
    remove_client(connecting_clients->data);

    client_pair->clients[1]->pair = client_pair;
    client_pair->clients[0]->pair = client_pair;

    initializeNewGame(client_pair);

    return client_pair;
}

void play_game(struct connection_data *con)
{
    // Initialize the game and other necessary game logic (SEG FAULT HERE)
    char gameState;
    char board_message[BUFSIZE];
    int beginLength = strlen(con->pair->clients[1 - con->index]->name) + 3;
    int len = snprintf(board_message, BUFSIZE, "BEGN|%d|%c|%s|\n", beginLength, con->role, con->pair->clients[1 - con->index]->name);
    if (write(con->fd, board_message, len) != len)
    {
        perror("write");
    }
    int output;
    // Game loop
    while (active)
    {
        output = process_player_move(con->pair, con->index);
        if (output == 1)
        {
            break;
        }
        else if(output == 2){
            free(con->pair);
            free(con);
            break;
        }
        

        con->pair->moves++;
        gameState = checkWinner(con->pair);

        // Report game state
        if (gameState == '.')
        {
            printf("%s\n", con->pair->board);
            if (con->pair->moves == 9)
            {
                snprintf(board_message, BUFSIZE, "OVER|26|D|Draw, the grid is full.|\n");
                write(con->pair->clients[1 - con->index]->fd, board_message, strlen(board_message));
                write(con->pair->clients[con->index]->fd, board_message, strlen(board_message));
                cleanup_and_close(con->pair, con->index, 1 - con->index);
                break;
            }
        }
        else
        {
            if (con->pair->currentTurn == 'X')
            {
                snprintf(board_message, BUFSIZE, "OVER|24|W|Tic-tac-toe, you win!|\n");
                write(con->pair->clients[con->index]->fd, board_message, strlen(board_message));
                int msgSize = strlen(con->pair->clients[1 - con->index]->name) + 22;
                snprintf(board_message, BUFSIZE, "OVER|%d|L|Tic-tac-toe, %s wins!|\n", msgSize, con->pair->clients[1 - con->index]->name);
                write(con->pair->clients[1 - con->index]->fd, board_message, strlen(board_message));
            }
            else
            {
                snprintf(board_message, BUFSIZE, "OVER|24|W|Tic-tac-toe, you win!|\n");
                write(con->pair->clients[con->index]->fd, board_message, strlen(board_message));
                int msgSize = strlen(con->pair->clients[1 - con->index]->name) + 22;
                snprintf(board_message, BUFSIZE, "OVER|%d|L|Tic-tac-toe, %s wins!|\n", msgSize, con->pair->clients[1 - con->index]->name);
                write(con->pair->clients[1 - con->index]->fd, board_message, strlen(board_message));
            }
            cleanup_and_close(con->pair, con->index, 1 - con->index);
            break;
        }
    }
}

/*
Reads data froma client thread, read_data is ran on seperate threads. Clients must go in the correct order which is 1st connection then second connection.
*/
void *read_data(void *arg)
{
    struct connection_data *con = arg;
    char buf[BUFSIZE + 1], host[HOSTSIZE], port[PORTSIZE];
    int bytes, error;
    error = getnameinfo(
        (struct sockaddr *)&con->addr, con->addr_len,
        host, HOSTSIZE,
        port, PORTSIZE,
        NI_NUMERICSERV);
    if (error)
    {
        fprintf(stderr, "getnameinfo: %s\n", gai_strerror(error));
        strcpy(host, "??");
        strcpy(port, "??");
    }
    printf("Connection from %s:%s\n", host, port);
    sigset_t mask;

    // Block the signals for the worker threads
    install_handlers(&mask);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);
    char board_message[BUFSIZE];

    bytes = read(con->fd, buf, BUFSIZE);
    if (bytes > 0)
    {
        buf[bytes] = '\0';
        remove_newline(buf);
        player_input parsedInputs = parse(buf);
        if (parsedInputs.type == PLAY)
        {
            error = add_username(parsedInputs.name);
            if (error)
            {
                // Bad username
                snprintf(board_message, BUFSIZE, "INVL|18|Username is taken|\n");
                write(con->fd, board_message, strlen(board_message));
                close(con->fd);
                free(con);
                return NULL;
            }
            else
            {
                pthread_mutex_lock(&connecting_mutex);
                strcpy(con->name, parsedInputs.name);
                con->wants_draw = 0;
                snprintf(board_message, BUFSIZE, "WAIT|0|\n");
                write(con->fd, board_message, strlen(board_message));
                add_client(con);

                if (numConnecting < 2)
                {
                    // If there are fewer than 2 clients, wait for another client to connect
                    con->index = 0;
                    waiting_client = con;
                    pthread_cond_wait(&connecting_cond, &connecting_mutex);
                }
                else
                {
                    // If there are at least 2 clients, create a new game, signal the waiting client, and reset numConnecting
                    con->index = 1;
                    client_pair_t *client_pair = create_game();
                    client_pair->gameOver = 0;

                    con->pair = client_pair;
                    waiting_client->pair = client_pair;
                    waiting_client = NULL;
                    pthread_cond_signal(&connecting_cond);
                }

                pthread_mutex_unlock(&connecting_mutex);
                struct sigaction sa;
                sa.sa_handler = thread_signal_handler;
                sa.sa_flags = 0;
                sigemptyset(&sa.sa_mask);
                if (sigaction(SIGUSR1, &sa, NULL) == -1)
                {
                    perror("sigaction");
                    return NULL;
                }
                play_game(con->pair->clients[1 - con->index]);
            }
        }
        else
        {
            // User didn't submit PLAY as first protocol
            snprintf(board_message, BUFSIZE, "INVL|24|Expected PLAY protocol.|\n");
            write(con->fd, board_message, strlen(board_message));
            close(con->fd);
            free(con);
            return NULL;
        }
    }
    else{
        close(con->fd);
        free(con);
    }

    return NULL;
}

int main(int argc, char **argv)
{
    pthread_mutex_init(&connecting_mutex, NULL);
    pthread_cond_init(&connecting_cond, NULL);
    sigset_t mask;
    struct connection_data *con;
    int error;
    pthread_t tid;

    char *service = argc == 2 ? argv[1] : "15000";
    install_handlers(&mask);

    int listener = open_listener(service, QUEUE_SIZE);
    if (listener < 0) // failed to bind server to requested port
        exit(EXIT_FAILURE);

    printf("Listening for incoming connections on %s\n", service);
    while (active)
    {
        con = (struct connection_data *)malloc(sizeof(struct connection_data));
        con->addr_len = sizeof(struct sockaddr_storage);
        con->fd = accept(listener, // accept() waits accepts incoming TCP requests and assigns it to a connection's file descriptor
                         (struct sockaddr *)&con->addr,
                         &con->addr_len);

        if (con->fd < 0)
        {
            perror("\naccept");
            free(con);
            // TODO check for specific error conditions (when the fuck does this happen)
            continue;
        }
        else
        {
            error = pthread_create(&tid, NULL, read_data, con); // creates client in separate thread
            if (error != 0)
            {
                fprintf(stderr, "pthread_create: %s\n", strerror(error));
                close(con->fd);
                free(con);
                continue;
            }
            else
            {
                con->thread_id = tid;
                pthread_detach(tid);
            }
        }
    }
    pthread_mutex_destroy(&connecting_mutex);
    pthread_cond_destroy(&connecting_cond);
    free_unique_names();

    puts("Shutting down");
    close(listener);
    return EXIT_SUCCESS;
