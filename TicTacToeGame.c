#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


typedef enum{
    PLAY,
    SUGDRAW,
    ACCDRAW,
    REJDRAW,
    RESIGN,
    MOVE,
    INVALID,
    BAD_COMMAND
} command_type;


#define MAX_ACTIVE_GAMES 200;

struct Game;

typedef struct player_input{
    command_type type;
    char name [257];
    char x_or_o; // X , 0
    char vertical_pos; //  1 , 2 , 3
    char horizontal_pos; // 1, 2 , 3
    char client_response_msg [100]; //
}player_input;

typedef struct game_manager{
    int total_active_games;
    struct Game* game_arr [200];
    char* is_active [200];
}game_manager;

typedef struct Client{
    char* name;
    char role;
    int socket;
}Client;

typedef struct Game{
    Client player1;
    Client player2;
    char board[10];
    char currentTurn;
}Game;




player_input parse(char* client_inp_str){
    player_input ret;
    memset(&ret, 0, sizeof(player_input));
    char* command_str = strtok(client_inp_str, "|");
    if (command_str == NULL) goto garbled; // input string is null
    char* msg_len_str = strtok(NULL, "|");
    if (msg_len_str == NULL) goto garbled; // input string has only one |
    int first_msg_char = strlen(msg_len_str) + strlen(command_str) + 2;

    int msg_len = strlen(client_inp_str + first_msg_char);

    if (atoi(msg_len_str) != msg_len || msg_len > 256){
        goto garbled; // message length is not given accurately or is too large
    }
        
    


    char* msg_pt1 = strtok(NULL, "|");
    if(msg_pt1 == NULL) goto process_inp_toks; // command is RSGN, or bad command
    char* msg_pt2 = strtok(NULL, "|"); // this is the 4th field, which is NULL for command PLAY
    
    process_inp_toks:
    
    if (strcmp(command_str, "PLAY") == 0){
        ret.type = PLAY;
        if (msg_pt1 == NULL) 
            goto garbled; // PLAY command without a name given
        strcpy((char*) &ret.name, msg_pt1); 
    }
    else if (strcmp(command_str, "DRAW") == 0){
        if (msg_pt1 == NULL) 
            goto garbled; // missing the third field for S,A,R
        if (msg_pt1[0] == 'R'){
            ret.type = REJDRAW;
        }else if (msg_pt1[0] == 'S'){
            ret.type = SUGDRAW;
        }else if (msg_pt1[0] == 'A'){
            ret.type = ACCDRAW;
        }else {
            ret.type = INVALID;
            strcpy((char*)&ret.client_response_msg, 
              "S to suggest draw, A to accept, R to reject\n");
        }
    }else if (strcmp(command_str, "RSGN") == 0){
        ret.type = RESIGN; // the command RSGN is always correct at this point
    }else if (strcmp(command_str, "MOVE") == 0){
        ret.type = MOVE;
        if (msg_pt1 == NULL || msg_pt2 == NULL)
            goto garbled; // missing X/O or missing coords
        if (msg_pt1[0] == 'X')
            ret.x_or_o = 'X';
        else if ((msg_pt1[0] == 'O')
            ret.x_or_o = 'O';
        else goto garbled; // player isn't X or O
        
        if (msg_pt2[0] != '1' && msg_pt2[0] != '2' && msg_pt2[0] != '3'
          ||msg_pt2[2] != '1' && msg_pt2[2] != '2' && msg_pt2[2] != '3')
        {
            strcpy((char* ) & ret.client_response_msg, 
             "Position must be in the form x,y with {1,2,3} for each\n");
            ret.type = INVALID;
        }
        ret.horizontal_pos = msg_pt2[0];
        ret.vertical_pos = msg_pt2[2];
    }
    else 
        goto garbled; // command not recognized
    if(0){
        garbled:
        strcpy((char*)ret.client_response_msg, "error, garbled command\n");
        ret.type = BAD_COMMAND;
    }
    return ret;
}
/*
void initializeNewGame(Game* gameInstance, Client p1, Client p2){
    //Initialize the board
    memcpy(gameInstance->board,".........\0", 10);
    gameInstance->currentTurn = 'X';

    //Assign players
    gameInstance->player1 = p1;
    gameInstance->player2 = p2;

    //Assign roles
    gameInstance->player1.role = 'X';
    gameInstance->player2.role = 'O';
}

int playerMove(Game* gameInstance, int x, int y){
    int location = x+((y-1)*3)-1;
    if(gameInstance->board[location] != '.') //Spot has already been taken
        return EXIT_FAILURE;
    else{
        gameInstance->board[location] = gameInstance->currentTurn;
        if(gameInstance->currentTurn == 'X')
            gameInstance->currentTurn = 'O';
        else
            gameInstance->currentTurn = 'X';

        return EXIT_SUCCESS;
    }
}

char checkWinner(Game* gameInstance) {
    char b[3][3];
    int k = 1;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            b[i][j] = gameInstance->board[k++];
        }
    }

    for (int i = 0; i < 3; i++) {
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
*/
int main(int argc, char* argv [])
{
    
    if (argc<2){
        printf("shit\n not enough inputs\n");
        return EXIT_SUCCESS;
    }

    size_t buff_size = 300;
    char temp [300] = {0};
    char* line_buffer = (char*) temp;

    FILE* in = fopen("client_inputs.txt", "r");
    

    while(getline(&line_buffer, &buff_size, in) > 0){
        line_buffer[strlen(line_buffer)-1] = 0;
        printf ("%s\n", line_buffer);
        player_input test_value = parse(line_buffer);
        if (test_value.name[0] != 0)
            printf("Name: %s\n", test_value.name);
        if (test_value.horizontal_pos != 0)
            printf("(%c,%c)\n", test_value.horizontal_pos, test_value.vertical_pos);
        printf("Command code %d  %s\n\n", test_value.type, test_value.client_response_msg);
        
    }
    /*
    Client player1;
    Client player2;
    player1.name = "Joe Biden";
    player2.name = "Hugh Ass";
    Game instance;
    initializeNewGame(&instance, player1, player2);

    char user_input[3];
    char gameState;
    int moves = 0;
    ssize_t bytes_read;
    while(1){
        //Get user input
        printf("%c's turn: ", instance.currentTurn);
        fflush(stdout);
        bytes_read = read(STDIN_FILENO, &user_input, 3);
        if(user_input[0] == 'q')
            return EXIT_SUCCESS;
        else if(bytes_read == 3) {
            read(STDIN_FILENO, NULL, 1);
            if(playerMove(&instance, user_input[0] - '0', user_input[2] - '0')){
                perror("That space is occupied.");
                return EXIT_FAILURE;
            }
            moves++;
            gameState = checkWinner(&instance);

            //Report game state
            if(gameState == '.'){
                printf("%s\n", instance.board);
                if(moves == 9){
                    printf("Draw!\n");
                    return EXIT_SUCCESS;
                }
            }
            else{
                if(instance.currentTurn == 'X')
                    printf("O wins!\n");
                else
                    printf("X wins!\n");
                return EXIT_SUCCESS;
            }
        } else {
            perror("Error reading input");
            return EXIT_FAILURE;
        }
    }
    */
    return EXIT_SUCCESS;
}
