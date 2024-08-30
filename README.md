Tic-Tac-Toe Online Concurrent games with interruption

Use the makefile by typing make

To launch the server, enter ./server [port_number]

To launch the client, enter ./client [host_name] [port_number]

<<Test Cases and Expected Outcomes>>
FYI: inp/1 is the message sent to the server, from the client with address "1" out/1 is the message sent to the client with address "1", from the server

I. Normal Functioning:

(A) First player sends PLAY command

    inp/1:  PLAY|5|DORK|

    out/1:  WAIT|0|

(B) Second active player sends PLAY command

    inp/2:  PLAY|5|NERD|

    out/1:  BEGN|7|X|NERD|
    out/2:  BEGN|7|O|DORK|

(C) Third active player sends PLAY command

    inp/3:  PLAY|5|GEEK|

    out/3:  WAIT|0|

(D) Fourth active player sends PLAY command

    inp/4:  PLAY|5|LOSR|

    out/3:  BEGN|7|X|LOSR|
    out/4:  BEGN|7|O|GEEK|

(E) Player resigns, at any point

    inp/2:  RSGN|0|

    out/1:  OVER|20|W|NERD has resigned|
    out/2:  OVER|20|L|You have resigned|

(F) Player suggests a draw, at any point

    inp/1:  DRAW|2|S|

    out/2:  DRAW|2|S|

(G) Player rejects a draw, at any point after the other player suggests a draw

    inp/1:  DRAW|2|R|

    out/2:  DRAW|2|R|

(H) Player accepts a draw, at any point after the other player suggests a draw

    inp/1:  DRAW|2|A|

    out/1:  OVER|26|D|Players agreed to draw.|
    out/2:  OVER|26|D|Players agreed to draw.|

(I) Game board filled, no winner

    inp/1:  MOVE|6|X|1,3|

    out/1:  OVER|26|D|Draw, the grid is full.|
    out/2:  OVER|26|D|Draw, the grid is full.|

(J) Player wins

    inp/1:  MOVE|6|X|1,3|

    out/1:  OVER|24|W|Tic-tac-toe, you win!|
    out/2:  OVER|26|L|Tic-tac-toe, DORK wins!|
II. Input error - application level

(A) Player tries to move, wrong turn

    inp/2:  MOVE|6|O|1,3|

    out/2:  INVL|21|It is not your turn.|

(B) Player sends a server output to the server

    inp/1:  OVER|12|L|You Lose!|

    out/1:  INVL|39|User command contains server protocol.|

(C) Player sends the wrong letter with DRAW command

    inp/1:  DRAW|2|K|

    out/1:  INVL|44|S to suggest draw, A to accept, R to reject|

(D) Player move on out-of-range coordinates

    inp/1:  MOVE|6|X|0,1|

    out/1:  INVL|55|Position must be in the form x,y with {1,2,3} for each|

(E) Player tries to move after suggesting a draw

    inp/1:  MOVE|6|X|1,1|

    out/1:  INVL|48|Waiting for opponent's reponse to draw request.|

(F) Player tries to start a new game during an active game

    inp/1:  PLAY|7|DILLON|

    out/1:  INVL|42|You cannot start a new game at this time.|

(G) Player tries to move after other player suggests a draw

    inp/1:  MOVE|6|X|2,2|

    out/1:  INVL|43|Draw request must be rejected or accepted.|

(H) Player X tries to place an O, or vice-versa

    inp/1:  MOVE|6|O|3,3|

    out/1:  INVL|25|Incorrect role selected.|

(I) Player tries to place a mark on an occupied sqaure

    inp/1:  MOVE|6|X|1,1|

    out/1:  INVL|24|That space is occupied.|

(J) Player suggests a draw after the other player suggested

    inp/1:  DRAW|2|S|

    out/1:  INVL|43|Draw request must be rejected or accepted.|
III. Input error - formating

In all of these cases, player 2 is sent the message:

OVER|{length}|W|{player name} gave bad input.|

(A) Player input is NULL

(B) Not enough fields

(C) Message too long

(D) Protocol not recognized

(E) Message too short
