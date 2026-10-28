#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#define GRID 8
#define SHIPS 3
#define LEN 3

/* panel widths (inside the borders) */
#define LEFT_W 24
#define MID_W 24
#define RIGHT_W 26
#define TOTAL_W 76

/* colors */
#define C_RESET "\033[0m"
#define C_DIM "\033[90m"
#define C_RED "\033[91m"
#define C_GREEN "\033[92m"
#define C_YELLOW "\033[93m"
#define C_BLUE "\033[94m"
#define C_CYAN "\033[96m"
#define C_WHITE "\033[97m"
#define C_AIM "\033[1;96m"

/* what each tile looks like: a symbol and a color */
#define HIT_CHAR 'X'
#define HIT_COLOR C_RED
#define MISS_CHAR 'o'
#define MISS_COLOR C_DIM
#define AIM_CHAR '@'
#define AIM_COLOR C_AIM
#define SHIP_CHAR '#'
#define SHIP_COLOR C_BLUE
#define WATER_CHAR '.'
#define WATER_COLOR C_DIM

/* keys */
#define K_UP 1
#define K_DOWN 2
#define K_LEFT 3
#define K_RIGHT 4
#define K_ENTER 5
#define K_OTHER 6

/* messages */
#define MSG_AIM "Use the arrow keys to aim, Enter to fire."
#define MSG_PLACE "Arrows move, Enter picks the start tile."

typedef struct {
    char name[32];
    int ship[GRID][GRID];
    char shot[GRID][GRID];
    int hits[SHIPS];
    int left;
    int landed;
    int points;
} Player;

Player players[2];
int turnNo = 1;
int vis = 0;

/* ---------- keyboard and terminal ---------- */

#ifndef _WIN32
struct termios oldTerm;

void rawOff(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &oldTerm);
}
#endif

/* Windows: turn on color codes. Mac and Linux: read keys without waiting for Enter. */
void setupTerminal(void) {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) {
        SetConsoleMode(h, mode | 0x0004);
    }
#else
    struct termios raw;
    tcgetattr(STDIN_FILENO, &oldTerm);
    raw = oldTerm;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    atexit(rawOff);
#endif
}

int getKey(void) {
#ifdef _WIN32
    int ch = _getch();
    if (ch == 13) {
        return K_ENTER;
    }
    if (ch == 0 || ch == 224) {
        ch = _getch();
        if (ch == 72) {
            return K_UP;
        }
        if (ch == 80) {
            return K_DOWN;
        }
        if (ch == 75) {
            return K_LEFT;
        }
        if (ch == 77) {
            return K_RIGHT;
        }
    }
    return K_OTHER;
#else
    char c, a, b;
    if (read(STDIN_FILENO, &c, 1) <= 0) {
        exit(0);
    }
    if (c == '\n' || c == '\r') {
        return K_ENTER;
    }
    if (c == 27) {
        if (read(STDIN_FILENO, &a, 1) <= 0) {
            return K_OTHER;
        }
        if (read(STDIN_FILENO, &b, 1) <= 0) {
            return K_OTHER;
        }
        if (a == '[' || a == 'O') {
            if (b == 'A') {
                return K_UP;
            }
            if (b == 'B') {
                return K_DOWN;
            }
            if (b == 'D') {
                return K_LEFT;
            }
            if (b == 'C') {
                return K_RIGHT;
            }
        }
    }
    return K_OTHER;
#endif
}

void waitEnter(void) {
    while (getKey() != K_ENTER) {
    }
}

void moveCursor(int key, int *r, int *c) {
    if (key == K_UP && *r > 0) {
        (*r)--;
    }
    if (key == K_DOWN && *r < GRID - 1) {
        (*r)++;
    }
    if (key == K_LEFT && *c > 0) {
        (*c)--;
    }
    if (key == K_RIGHT && *c < GRID - 1) {
        (*c)++;
    }
}

/* ---------- drawing tools ---------- */

void clearScreen(void) {
    printf("\033[2J\033[H");
}

/* prints colored text and counts how many characters are visible */
void put(const char *color, const char *fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("%s%s%s", color, buf, C_RESET);
    vis += (int)strlen(buf);
}

/* pads the line with spaces up to the panel width */
void fill(int width) {
    while (vis < width) {
        putchar(' ');
        vis++;
    }
    vis = 0;
}

void border(char ch) {
    printf(C_DIM "%c" C_RESET, ch);
}

void dashes(int n) {
    printf(C_DIM);
    while (n > 0) {
        putchar('-');
        n--;
    }
    printf(C_RESET);
}

/* a full line like +-----+-----+----+ (use 0 for a width that is not needed) */
void hline(int w1, int w2, int w3) {
    border('+');
    dashes(w1);
    border('+');
    if (w2 > 0) {
        dashes(w2);
        border('+');
    }
    if (w3 > 0) {
        dashes(w3);
        border('+');
    }
    printf("\n");
}

/* ---------- panels ---------- */

/* one tile on a grid: a colored symbol and a space */
void tile(const char *color, char symbol) {
    put(color, "%c ", symbol);
}

/* one item of a legend, like "X hit" */
void legend(const char *color, char symbol, const char *text) {
    put(color, " %c", symbol);
    put(C_DIM, " %s ", text);
}

/* one row of a grid: the row number and 8 tiles */
void gridRow(Player *p, int reveal, int r, int curR, int curC) {
    int c;
    put(C_DIM, " %d ", r + 1);
    for (c = 0; c < GRID; c++) {
        if (r == curR && c == curC) {
            tile(AIM_COLOR, AIM_CHAR);
        } else if (p->shot[r][c] == HIT_CHAR) {
            tile(HIT_COLOR, HIT_CHAR);
        } else if (p->shot[r][c] == MISS_CHAR) {
            tile(MISS_COLOR, MISS_CHAR);
        } else if (reveal == 1 && p->ship[r][c] >= 0) {
            tile(SHIP_COLOR, SHIP_CHAR);
        } else {
            tile(WATER_COLOR, WATER_CHAR);
        }
    }
}

/* one row of the left panel (your fleet) or the middle panel (targeting grid) */
void gridPanel(Player *p, int yours, int mode, int i, int curR, int curC) {
    int c;

    if (i == 0) {
        if (yours == 1) {
            put(C_CYAN, " YOUR FLEET");
        } else {
            put(C_RED, " TARGETING GRID");
        }
    } else if (i == 1) {
        if (yours == 1) {
            put(C_DIM, " enemy shots at you");
        } else if (mode == 1) {
            put(C_DIM, " arrows aim, Enter fire");
        } else {
            put(C_DIM, " battle starts soon");
        }
    } else if (i == 3) {
        put(C_DIM, "   ");
        for (c = 0; c < GRID; c++) {
            put(C_DIM, "%c ", 'A' + c);
        }
    } else if (i >= 4 && i <= 11) {
        gridRow(p, yours, i - 4, curR, curC);
    } else if (i == 13) {
        if (yours == 1) {
            legend(SHIP_COLOR, SHIP_CHAR, "ship");
            legend(HIT_COLOR, HIT_CHAR, "hit");
            legend(MISS_COLOR, MISS_CHAR, "miss");
        } else {
            legend(HIT_COLOR, HIT_CHAR, "hit");
            legend(MISS_COLOR, MISS_CHAR, "miss");
            legend(AIM_COLOR, AIM_CHAR, "aim");
        }
    }

    if (yours == 1) {
        fill(LEFT_W);
    } else {
        fill(MID_W);
    }
}

/* a fleet line like "Ship 1  ==="; each hit turns one = red */
void shipRow(Player *p, int n) {
    int k;
    put(C_WHITE, " Ship %d  ", n + 1);
    for (k = 0; k < LEN; k++) {
        if (k < p->hits[n]) {
            put(C_RED, "=");
        } else {
            put(C_DIM, "=");
        }
    }
    if (p->hits[n] == LEN) {
        put(C_YELLOW, "  sunk");
    }
}

/* one row of the right panel: stats and fleets */
void rightRow(int cur, int i) {
    Player *me = &players[cur];
    Player *enemy = &players[1 - cur];

    if (i == 0) {
        put(C_DIM, " %-7s%-6s%s", "POINTS", "HITS", "SHIPS LEFT");
    } else if (i == 1) {
        put(C_CYAN, " %-7d", me->points);
        put(C_RED, "%-6d", me->landed);
        put(C_GREEN, "%d/%d", me->left, SHIPS);
    } else if (i == 4) {
        put(C_YELLOW, " ENEMY FLEET  ");
        put(C_GREEN, "%d/%d afloat", enemy->left, SHIPS);
    } else if (i >= 5 && i <= 7) {
        shipRow(enemy, i - 5);
    } else if (i == 10) {
        put(C_YELLOW, " YOUR FLEET  ");
        put(C_GREEN, "%d/%d afloat", me->left, SHIPS);
    } else if (i >= 11 && i <= 13) {
        shipRow(me, i - 11);
    }
    fill(RIGHT_W);
}

/* the title bar: game name, status on the left, players and turn on the right */
void titleBar(const char *status) {
    char a[16], b[16], t[24];
    int rlen;

    sprintf(a, "%.8s", players[0].name);
    sprintf(b, "%.8s", players[1].name);
    sprintf(t, "turn %d", turnNo);
    rlen = (int)(strlen(a) + 4 + strlen(b) + 2 + strlen(t));

    border('|');
    put(C_CYAN, " BATTLEGRID   ");
    put(C_YELLOW, "[ * %s ]", status);
    while (vis < TOTAL_W - rlen - 1) {
        putchar(' ');
        vis++;
    }
    put(C_CYAN, "%s", a);
    put(C_DIM, " vs ");
    put(C_RED, "%s", b);
    put(C_DIM, "  %s", t);
    fill(TOTAL_W);
    border('|');
    printf("\n");
}

/*
 * mode 0 = placing ships (cursor is on your fleet)
 * mode 1 = battle (cursor is on the targeting grid)
 * a cursor of -1 means no cursor is shown
 */
void drawScreen(int cur, int mode, int curR, int curC, const char *status, const char *msg) {
    int i;
    int leftR = -1, leftC = -1, midR = -1, midC = -1;

    if (mode == 0) {
        leftR = curR;
        leftC = curC;
    } else {
        midR = curR;
        midC = curC;
    }

    printf("\033[H");
    hline(TOTAL_W, 0, 0);
    titleBar(status);
    hline(LEFT_W, MID_W, RIGHT_W);

    for (i = 0; i <= 13; i++) {
        char sep = '|';
        if (i == 3 || i == 9) {
            sep = '+';
        }

        border('|');
        gridPanel(&players[cur], 1, mode, i, leftR, leftC);
        border('|');
        gridPanel(&players[1 - cur], 0, mode, i, midR, midC);
        border(sep);
        if (sep == '+') {
            dashes(RIGHT_W);
        } else {
            rightRow(cur, i);
        }
        border(sep);
        printf("\n");
    }

    /* message bar */
    hline(LEFT_W, MID_W, RIGHT_W);
    border('|');
    put(C_CYAN, " > ");
    put(C_WHITE, "%.70s", msg);
    fill(TOTAL_W);
    border('|');
    printf("\n");
    hline(TOTAL_W, 0, 0);
}

/* ---------- game ---------- */

void initPlayer(Player *p) {
    int r, c, i;
    for (r = 0; r < GRID; r++) {
        for (c = 0; c < GRID; c++) {
            p->ship[r][c] = -1;
            p->shot[r][c] = ' ';
        }
    }
    for (i = 0; i < SHIPS; i++) {
        p->hits[i] = 0;
    }
    p->left = SHIPS;
    p->landed = 0;
    p->points = 0;
}

/* places a ship if it fits. Returns 1 if placed, 0 if it does not fit. */
int placeShip(Player *p, int id, int r, int c, int horiz) {
    int i, y, x;
    int dr = 1, dc = 0;

    if (horiz == 1) {
        dr = 0;
        dc = 1;
    }
    for (i = 0; i < LEN; i++) {
        y = r + dr * i;
        x = c + dc * i;
        if (y >= GRID || x >= GRID) {
            return 0;
        }
        if (p->ship[y][x] >= 0) {
            return 0;
        }
    }
    for (i = 0; i < LEN; i++) {
        p->ship[r + dr * i][c + dc * i] = id;
    }
    return 1;
}

/* the player moves the cursor with the arrow keys until they press Enter */
void pickTile(int cur, int mode, int *r, int *c, const char *status, const char *msg, const char *defaultMsg) {
    int key;
    while (1) {
        drawScreen(cur, mode, *r, *c, status, msg);
        msg = defaultMsg;
        key = getKey();
        if (key == K_ENTER) {
            return;
        }
        moveCursor(key, r, c);
    }
}

/* shows a message with no cursor and waits for Enter */
void showMessage(int cur, int mode, const char *status, const char *msg) {
    drawScreen(cur, mode, -1, -1, status, msg);
    waitEnter();
}

void setup(int cur) {
    Player *p = &players[cur];
    int id;
    char status[40];

    clearScreen();
    for (id = 0; id < SHIPS; id++) {
        int curR = 0, curC = 0;
        const char *msg = MSG_PLACE;
        sprintf(status, "%.8s: place ship %d/%d", p->name, id + 1, SHIPS);

        while (1) {
            int key, horiz;
            pickTile(cur, 0, &curR, &curC, status, msg, MSG_PLACE);

            drawScreen(cur, 0, curR, curC, status, "Right = across, Down = down, Enter = back");
            do {
                key = getKey();
            } while (key != K_RIGHT && key != K_DOWN && key != K_ENTER);
            if (key == K_ENTER) {
                msg = MSG_PLACE;
                continue;
            }

            horiz = 0;
            if (key == K_RIGHT) {
                horiz = 1;
            }
            if (placeShip(p, id, curR, curC, horiz) == 1) {
                break;
            }
            msg = "It does not fit there. Try another spot.";
        }
    }
    sprintf(status, "%.8s: fleet ready", p->name);
    showMessage(cur, 0, status, "Fleet ready. Press Enter to hide the board.");
    clearScreen();
}

/* returns 1 if this player won */
int turn(int cur) {
    Player *me = &players[cur];
    Player *enemy = &players[1 - cur];
    int curR = 0, curC = 0;
    const char *msg = MSG_AIM;
    char status[40];

    sprintf(status, "%.8s's turn", me->name);
    clearScreen();

    while (1) {
        int id, result;
        pickTile(cur, 1, &curR, &curC, status, msg, MSG_AIM);
        msg = MSG_AIM;

        if (enemy->shot[curR][curC] != ' ') {
            msg = "Already fired there. Pick another tile.";
            continue;
        }

        id = enemy->ship[curR][curC];
        if (id < 0) {
            enemy->shot[curR][curC] = MISS_CHAR;
            showMessage(cur, 1, status, "Miss. Press Enter to end your turn.");
            return 0;
        }

        enemy->shot[curR][curC] = HIT_CHAR;
        enemy->hits[id]++;
        me->landed++;
        result = 1;
        if (enemy->hits[id] == LEN) {
            enemy->left--;
            result = 2;
        }

        if (enemy->left == 0) {
            drawScreen(cur, 1, -1, -1, status, "Last ship sunk!");
            return 1;
        }
        if (result == 2) {
            showMessage(cur, 1, status, "Ship sunk! Press Enter to fire again.");
        } else {
            showMessage(cur, 1, status, "Hit! Press Enter to fire again.");
        }
    }
}

int main(void) {
    int current = 0, i;

    printf("BATTLEGRID: Tactical Strike\n\n");
    printf("Tip: use a terminal at least 80 wide and 24 tall.\n\n");
    for (i = 0; i < 2; i++) {
        printf("Player %d name: ", i + 1);
        if (fgets(players[i].name, sizeof(players[i].name), stdin) == NULL) {
            players[i].name[0] = '\0';
        }
        players[i].name[strcspn(players[i].name, "\r\n")] = '\0';
        if (players[i].name[0] == '\0') {
            sprintf(players[i].name, "Player %d", i + 1);
        }
        initPlayer(&players[i]);
    }

    setupTerminal();
    setup(0);
    setup(1);

    while (turn(current) == 0) {
        current = 1 - current;
        turnNo++;
    }

    printf("\n%s wins the battle!\n", players[current].name);
    return 0;
}
