// DO NOT MODIFY THIS FILE!

#include <stdlib.h>

#define EXPEDITION_CHAR 'S'
#define TRAIL_CHAR '='
#define EMPTY_CHAR ' '
#define COMMAND_CHAR '*'
#define KARRAMBA_CHAR '.'
#define NEWLINE '\n'

// Parameters name convention used in the following functions:
// `board` -- An array containing a board initialized with `fill_board`. Must be of size at least `m*(n+1)`.
// `n`, `m` -- Width and height of a board, respectively.
// `x`, `y` -- Column and row (respectively) index of a position on a board (`0 <= x <= n-1`, `0 <= y <= m-1`).
// `px`, `py` -- Pointers to variables containing column and row index.
// `move` -- One of the `W`, `A`, `S`, `D` characters, indicating the move (up, left, down, and right, respectively).

// Initializes a board of size `m x n`, i.e. fills with sequence of spaces and newline to make the board human-readable.
// `board` must be of size at least `m*(n+1)`.
void fill_board(char* board, int n, int m);

// In a board of size `m x n`, sets a character located at `(y,x)` to `c`.
void set_char(char* board, int x, int y, int n, int m, char c);

// In a board of size `m x n`, gets a character located at `(y,x)`.
char get_char(char* board, int x, int y, int n, int m);

// In a board of size `m x n`, changes the position stored in `*px` and `*py`, according to a specified `move`.
// The given move will not be performed if it is unable to do it (e.g. position would go outside a board).
// NOTE: `px` and `py` are the pointers to current position coordinates!
void move_pos(char* board, char move, int n, int m, int* px, int* py);

// Gets the random move (one of the characters: `W`, `A`, `S`, `D`) that can be currently performed.
// Use this function in Expedition's code.
char get_random_move(char* board, int x, int y, int n, int m);

// Checks if there is a neighbouring field containing trail character (`=`).
// Use this function in Don Pedro's code to indicate if a message to the terminal has to be printed.
// Returns a non-zero integer if the trail exists, and zero otherwise.
int has_trail(char* board, int x, int y, int n, int m);

// Finds a neighbouring field containing trail character (`=`) and returns a move (`WASD`) following the trail.
// If no trail is found, returns a random move. Returned move should be able to be currently performed.
// Use this function in Don Pedro's code.
char get_trail_move(char* board, int x, int y, int n, int m);

void fill_board(char* board, int n, int m)
{
    for (int i = 0; i < m; ++i)
    {
        for (int j = 0; j < n; ++j)
        {
            board[i * (n + 1) + j] = EMPTY_CHAR;
        }
        board[i * (n + 1) + n] = NEWLINE;
    }
}

void set_char(char* board, int x, int y, int n, int m, char c)
{
    board[y * (n + 1) + x] = c;
}

char get_char(char* board, int x, int y, int n, int m)
{
    return board[y * (n + 1) + x];
}

void move_pos(char* board, char move, int n, int m, int* x, int* y)
{
    switch (move)
    {
        case 'W':
            if (*y > 0)
                --(*y);
            break;
        case 'S':
            if (*y < m - 1)
                ++(*y);
            break;
        case 'A':
            if (*x > 0)
                --(*x);
            break;
        case 'D':
            if (*x < n - 1)
                ++(*x);
            break;
    }
}

// Internal function -- you do not have to use it in your solution.
int _get_moves(char* board, int x, int y, int n, int m, char* moves)
{
    int count = 0;
    if (x > 0)
        moves[count++] = 'A';
    if (x < n - 1)
        moves[count++] = 'D';
    if (y > 0)
        moves[count++] = 'W';
    if (y < m - 1)
        moves[count++] = 'S';
    return count;
}

// Internal function -- you do not have to use it in your solution.
int _get_trail_moves(char* board, int x, int y, int n, int m, char* moves)
{
    int count = 0;
    if (x > 0 && get_char(board, x - 1, y, n, m) == TRAIL_CHAR)
        moves[count++] = 'A';
    if (x < n - 1 && get_char(board, x + 1, y, n, m) == TRAIL_CHAR)
        moves[count++] = 'D';
    if (y > 0 && get_char(board, x, y - 1, n, m) == TRAIL_CHAR)
        moves[count++] = 'W';
    if (y < m - 1 && get_char(board, x, y + 1, n, m) == TRAIL_CHAR)
        moves[count++] = 'S';
    return count;
}

char get_random_move(char* board, int x, int y, int n, int m)
{
    char available_moves[4];
    int count = _get_moves(board, x, y, n, m, available_moves);
    return available_moves[rand() % count];
}

int has_trail(char* board, int x, int y, int n, int m)
{
    char available_moves[4];
    int count = _get_trail_moves(board, x, y, n, m, available_moves);
    return count;
}

char get_trail_move(char* board, int x, int y, int n, int m)
{
    char available_moves[4];
    int count = _get_trail_moves(board, x, y, n, m, available_moves);
    if (count > 0)
        return available_moves[rand() % count];
    return get_random_move(board, x, y, n, m);
}