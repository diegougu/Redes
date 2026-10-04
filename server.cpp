/* Server code in C++ - Intermediary Chat Server */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <algorithm>
#include <set>
#include <random>

using namespace std;

map<string, int> ListOfCli;
mutex mtx;

/* -------------------------------------------------------------------- */
/* TIC TAC TOE: todo el estado vive en el SERVIDOR (todo es "global").   */
/* El cliente solo manda movimientos y redibuja lo que el servidor dice. */
/* Todas las funciones de abajo asumen que mtx YA esta tomado.           */
/* -------------------------------------------------------------------- */
struct TicTacToe
{
    string players[2];                 // players[0] juega 'X', players[1] juega 'O'
    const char symbols[2] = {'X', 'O'};
    string board = string(9, '-');     // 9 bytes: 'X', 'O' o '-' (vacio)
    int turn = 0;                      // indice del jugador que debe mover
    bool playing = false;              // true cuando hay 2 jugadores y partida activa
    set<string> viewers;               // nicknames que solo observan
} game;

void sendRaw(int fd, const string &frame)
{
    write(fd, frame.data(), frame.size());
}

void sendToNick(const string &nick, const string &frame)
{
    auto it = ListOfCli.find(nick);
    if (it != ListOfCli.end())
        sendRaw(it->second, frame);
}

/* Envia a los dos jugadores y a todos los viewers */
void sendToGame(const string &frame)
{
    for (int i = 0; i < 2; i++)
        if (!game.players[i].empty())
            sendToNick(game.players[i], frame);
    for (const string &v : game.viewers)
        sendToNick(v, frame);
}

/* Error ser->cli: 'e' + 1 byte de codigo
   '1' posicion ocupada | '2' no es tu turno | '3' posicion invalida
   '4' no estas en una partida | '5' no puedes unirte (ya inscrito / partida en curso) */
void sendGameError(int fd, char code)
{
    string f = "e";
    f += code;
    sendRaw(fd, f);
    cout << "[TRAMA ENVIADA] e | " << code << endl;
}

void sendBoardTo(int fd)
{
    sendRaw(fd, "T" + game.board);
}

/* Devuelve 'X' u 'O' si alguien gano, 'D' si hay empate, 0 si sigue el juego */
char checkResult()
{
    static const int L[8][3] = {{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
    for (auto &l : L)
        if (game.board[l[0]] != '-' &&
            game.board[l[0]] == game.board[l[1]] &&
            game.board[l[1]] == game.board[l[2]])
            return game.board[l[0]];

    if (game.board.find('-') == string::npos)
        return 'D';
    return 0;
}

void resetGame()
{
    game.players[0].clear();
    game.players[1].clear();
    game.board = string(9, '-');
    game.turn = 0;
    game.playing = false;
}

/* Game over ser->cli: 'G' + 1 byte ('X', 'O' o 'D' = empate) */
void endGame(char result)
{
    string f = "G";
    f += result;
    sendToGame(f);
    cout << "[TRAMA ENVIADA] G | " << result << endl;
    resetGame();
}

/* Tu turno ser->cli: 'U' + simbolo del jugador */
void sendTurn()
{
    string f = "U";
    f += game.symbols[game.turn];
    sendToNick(game.players[game.turn], f);
    cout << "[TRAMA ENVIADA] U | " << game.symbols[game.turn] << " (" << game.players[game.turn] << ")" << endl;
}

string zeroPad(long long number, int size)
{
    string str = to_string(number);

    if ((int)str.length() >= size)
        return str;

    return string(size - str.length(), '0') + str;
}


long readExact(int S, char *buff, long len)
{
    long totalRead = 0;
    while (totalRead < len)
    {
        long n = read(S, buff + totalRead, len - totalRead);
        if (n <= 0)
            return n; // socket cerrado o error
        totalRead += n;
    }
    return totalRead;
}

void removeClient(const string &nickname, int S)
{
    lock_guard<mutex> lock(mtx);
    ListOfCli.erase(nickname);
    close(S);

    /* Limpieza del juego si el que se fue era viewer o jugador */
    game.viewers.erase(nickname);
    for (int i = 0; i < 2; i++)
    {
        if (game.players[i] == nickname)
        {
            if (game.playing)
                endGame(game.symbols[1 - i]); // gana el otro por abandono
            else
                resetGame();                  // estaba esperando oponente
            break;
        }
    }
}

/* -------------------------------------------------------------------- */
/* Thread que atiende a un cliente conectado                            */
/* -------------------------------------------------------------------- */
void ThreadReadClient(int S)
{
    string nickname;
    char buff[1024];
    int n, tamano;

    /* --- Esperar / registrar Action 'N' --- */
    n = readExact(S, buff, 1);
    if (n <= 0) { close(S); return; }

    while (buff[0] != 'N')
    {
        n = readExact(S, buff, 1);
        if (n <= 0) { close(S); return; }
    }

    n = readExact(S, buff, 7);
    if (n <= 0) { close(S); return; }
    buff[n] = '\0';
    tamano = atoi(buff);

    n = readExact(S, buff, tamano);
    if (n <= 0) { close(S); return; }
    buff[n] = '\0';
    nickname = buff;

    {
        lock_guard<mutex> lock(mtx);
        ListOfCli[nickname] = S;
    }
    cout << "[TRAMA RECIBIDA] N | " << zeroPad(nickname.size(), 7) << " | " << nickname << endl;
    cout << "[+] Cliente registrado: " << nickname << " (fd=" << S << ")" << endl;

    string destination;
    string dataStructure;
    string msg;
    bool running = true;

    while (running)
    {
        n = readExact(S, buff, 1);
        if (n <= 0) break; // cliente se desconecto sin mandar 'Q'

        if (buff[0] == 'M')
        {
            n = readExact(S, buff, 7);
            if (n <= 0) break;
            buff[n] = '\0';
            tamano = atoi(buff);

            n = readExact(S, buff, tamano);
            if (n <= 0) break;
            buff[n] = '\0';
            destination = buff;

            n = readExact(S, buff, 11);
            if (n <= 0) break;
            buff[n] = '\0';
            tamano = atoi(buff);

            n = readExact(S, buff, tamano);
            if (n <= 0) break;
            buff[n] = '\0';
            msg = buff;

            cout << "[TRAMA RECIBIDA] M | " << zeroPad(destination.size(), 7) << " | " << destination
                 << " | " << zeroPad(msg.size(), 11) << " | " << msg << endl;

            dataStructure.clear();
            dataStructure += 'm';
            dataStructure += zeroPad(nickname.size(), 7);
            dataStructure += nickname; // remitente, para que el destino sepa quien escribe
            dataStructure += zeroPad(msg.size(), 11);
            dataStructure += msg;

            cout << "[TRAMA REENVIADA] m | " << zeroPad(nickname.size(), 7) << " | " << nickname
                 << " | " << zeroPad(msg.size(), 11) << " | " << msg << endl;

            lock_guard<mutex> lock(mtx);
            auto it = ListOfCli.find(destination);
            if (it != ListOfCli.end())
            {
                write(it->second, dataStructure.c_str(), dataStructure.size());
            }
            else
            {
                cout << "[!] Destino no encontrado: " << destination << endl;
                dataStructure.clear();
                dataStructure += 'E';
                dataStructure += zeroPad(msg.size(), 11);
                dataStructure += msg;
                write(S, dataStructure.c_str(), dataStructure.size());
            }
        }
        else if (buff[0] == 'B')
        {
            n = readExact(S, buff, 11);
            if (n <= 0) break;
            buff[n] = '\0';
            tamano = atoi(buff);

            n = readExact(S, buff, tamano);
            if (n <= 0) break;
            buff[n] = '\0';
            msg = buff;

            cout << "[TRAMA RECIBIDA] B | " << zeroPad(msg.size(), 11) << " | " << msg << endl;

            dataStructure.clear();
            dataStructure += 'b';
            dataStructure += zeroPad(nickname.size(), 7);
            dataStructure += nickname;
            dataStructure += zeroPad(msg.size(), 11);
            dataStructure += msg;

            cout << "[TRAMA REENVIADA] b | " << zeroPad(nickname.size(), 7) << " | " << nickname
                 << " | " << zeroPad(msg.size(), 11) << " | " << msg << endl;

            lock_guard<mutex> lock(mtx);
            for (auto it = ListOfCli.begin(); it != ListOfCli.end(); ++it)
            {
                write(it->second, dataStructure.c_str(), dataStructure.size());
            }
        }
        else if (buff[0] == 'F')
        {
            /* Formato Cli->Ser: F | size13 destino | destino | size13 nombreArchivo | nombreArchivo | size25 tamanoArchivo | archivo */
            n = readExact(S, buff, 13);
            if (n <= 0) break;
            buff[n] = '\0';
            int destSize = atoi(buff);

            string destination(destSize, '\0');
            if (destSize > 0)
            {
                n = readExact(S, &destination[0], destSize);
                if (n <= 0) break;
            }

            n = readExact(S, buff, 13);
            if (n <= 0) break;
            buff[n] = '\0';
            int filenameSize = atoi(buff);

            string filename(filenameSize, '\0');
            if (filenameSize > 0)
            {
                n = readExact(S, &filename[0], filenameSize);
                if (n <= 0) break;
            }

            n = readExact(S, buff, 25);
            if (n <= 0) break;
            buff[n] = '\0';
            long long fileSize = atoll(buff);

            cout << "[TRAMA RECIBIDA] F | " << zeroPad(destination.size(), 13) << " | " << destination
                 << " | " << zeroPad(filename.size(), 13) << " | " << filename
                 << " | " << zeroPad(fileSize, 25) << " | (" << fileSize << " bytes)" << endl;

            /* IMPORTANTE: el cuerpo del archivo NUNCA se carga completo a memoria.
               Se reenvia en chunks directamente del socket del emisor al del
               destinatario (streaming), para soportar archivos de varios GB sin
               agotar la RAM del proceso. */
            int destFd = -1;
            {
                lock_guard<mutex> lock(mtx);
                auto it = ListOfCli.find(destination);
                if (it != ListOfCli.end())
                    destFd = it->second;
            }

            const size_t CHUNK = 1 << 20; // 1 MB
            vector<char> chunkBuf(CHUNK);

            if (destFd != -1)
            {
                /* Se envia primero el encabezado Ser->Cli: F | size13 origen | origen |
                   size13 nombreArchivo | nombreArchivo | size25 tamano, y luego se
                   transmite el cuerpo en chunks a medida que llega del emisor. */
                string header;
                header += 'F';
                header += zeroPad(nickname.size(), 13);
                header += nickname; // origen: quien envia el archivo
                header += zeroPad(filename.size(), 13);
                header += filename;
                header += zeroPad(fileSize, 25);
                write(destFd, header.data(), header.size());

                long long remaining = fileSize;
                bool relayOk = true;
                while (remaining > 0)
                {
                    long toRead = (long)min((long long)CHUNK, remaining);
                    long got = readExact(S, chunkBuf.data(), toRead);
                    if (got <= 0) { relayOk = false; break; }

                    long written = 0;
                    while (written < got)
                    {
                        ssize_t w = write(destFd, chunkBuf.data() + written, got - written);
                        if (w <= 0) { relayOk = false; break; }
                        written += w;
                    }
                    if (!relayOk) break;
                    remaining -= got;
                }

                if (!relayOk) break; // se perdio la conexion a mitad de la transferencia

                cout << "[TRAMA REENVIADA] F | " << zeroPad(nickname.size(), 13) << " | " << nickname
                     << " | " << zeroPad(filename.size(), 13) << " | " << filename
                     << " | " << zeroPad(fileSize, 25) << " | (" << fileSize << " bytes)" << endl;
            }
            else
            {
                cout << "[!] Destino no encontrado para archivo: " << destination << endl;

                /* Aunque no haya destino, se deben drenar los bytes del archivo que ya
                   viajan en el socket, o se perderia la sincronia con la siguiente trama. */
                long long remaining = fileSize;
                while (remaining > 0)
                {
                    long toRead = (long)min((long long)CHUNK, remaining);
                    long got = readExact(S, chunkBuf.data(), toRead);
                    if (got <= 0) break;
                    remaining -= got;
                }

                string errMsg = "Archivo '" + filename + "' no enviado. Usuario no conectado: " + destination;
                string errFrame;
                errFrame += 'E';
                errFrame += zeroPad((long long)errMsg.size(), 11);
                errFrame += errMsg;
                write(S, errFrame.c_str(), errFrame.size());
            }
        }
        else if (buff[0] == 'L')
        {
            /* Se construye un CSV con los nicknames conectados actualmente. */
            string csv;
            {
                lock_guard<mutex> lock(mtx);
                for (auto it = ListOfCli.begin(); it != ListOfCli.end(); ++it)
                {
                    if (!csv.empty())
                        csv += ',';
                    csv += it->first;
                }

                /* La lista se devuelve solo al cliente que hizo la solicitud. */
                dataStructure.clear();
                dataStructure += 'L';
                dataStructure += zeroPad(csv.size(), 11);
                dataStructure += csv;
                write(S, dataStructure.c_str(), dataStructure.size());
            }

            cout << "[TRAMA RECIBIDA] L" << endl;
            cout << "[TRAMA ENVIADA] L | " << zeroPad(csv.size(), 11)
                 << " | " << csv << endl;
        }
        else if (buff[0] == 'P')
        {
            /* Cli->Ser: 'P' = quiero jugar. Sin payload. */
            cout << "[TRAMA RECIBIDA] P (" << nickname << ")" << endl;
            lock_guard<mutex> lock(mtx);

            if (game.playing || game.players[0] == nickname)
            {
                sendGameError(S, '5');
            }
            else if (game.players[0].empty())
            {
                game.viewers.erase(nickname);
                game.players[0] = nickname;
                sendRaw(S, "W"); // Ser->Cli: esperando oponente
            }
            else
            {
                /* Segundo jugador: arranca la partida */
                game.viewers.erase(nickname);
                game.players[1] = nickname;
                game.playing = true;

                /* El servidor elige al azar quien empieza */
                static mt19937 rng(random_device{}());
                game.turn = rng() % 2;

                /* 'S' + simbolo: le dice a cada jugador con que juega */
                for (int i = 0; i < 2; i++)
                {
                    string f = "S";
                    f += game.symbols[i];
                    sendToNick(game.players[i], f);
                }
                sendToGame("T" + game.board);
                sendTurn();
                cout << "[*] Partida iniciada: " << game.players[0] << " (X) vs " << game.players[1] << " (O)" << endl;
            }
        }
        else if (buff[0] == 'V')
        {
            /* Cli->Ser: 'V' = quiero ver la partida (viewer). Sin payload. */
            cout << "[TRAMA RECIBIDA] V (" << nickname << ")" << endl;
            lock_guard<mutex> lock(mtx);

            if (game.players[0] == nickname || game.players[1] == nickname)
            {
                sendGameError(S, '5');
            }
            else
            {
                game.viewers.insert(nickname);
                sendBoardTo(S); // el viewer recibe el tablero actual de inmediato
            }
        }
        else if (buff[0] == 'X')
        {
            /* Cli->Ser: 'X' + 1 byte con la posicion '1'..'9' (el movimiento) */
            n = readExact(S, buff, 1);
            if (n <= 0) break;
            char posChar = buff[0];
            cout << "[TRAMA RECIBIDA] X | " << posChar << " (" << nickname << ")" << endl;

            lock_guard<mutex> lock(mtx);

            int idx = (game.players[0] == nickname) ? 0 : (game.players[1] == nickname ? 1 : -1);
            int pos = posChar - '1';

            if (!game.playing || idx == -1)
                sendGameError(S, '4');
            else if (idx != game.turn)
                sendGameError(S, '2');
            else if (pos < 0 || pos > 8)
                sendGameError(S, '3');
            else if (game.board[pos] != '-')
                sendGameError(S, '1'); // el cliente ya valida esto, pero el servidor es la autoridad
            else
            {
                game.board[pos] = game.symbols[idx];
                sendToGame("T" + game.board); // el servidor redibuja y manda la tabla a TODOS

                char result = checkResult();
                if (result)
                    endGame(result);
                else
                {
                    game.turn = 1 - game.turn;
                    sendTurn();
                }
            }
        }
        else if (buff[0] == 'Q')
        {
            running = false;
        }
        /* cualquier otro byte de accion desconocido se ignora */
    }

    removeClient(nickname, S);
    cout << "[-] Cliente desconectado: " << nickname << endl;
}

int main(int argc, char *argv[])
{
    int port = 45000;
    if (argc >= 2)
    {
        port = atoi(argv[1]);
        if (port <= 0) port = 45000;
    }

    struct sockaddr_in stSockAddr;
    int ServerSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (-1 == ServerSocket)
    {
        perror("can not create socket");
        exit(EXIT_FAILURE);
    }

    /* Permite reusar el puerto rapido tras reiniciar el servidor */
    int opt = 1;
    setsockopt(ServerSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(port);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if (-1 == bind(ServerSocket, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in)))
    {
        perror("error bind failed");
        close(ServerSocket);
        exit(EXIT_FAILURE);
    }

    if (-1 == listen(ServerSocket, 10))
    {
        perror("error listen failed");
        close(ServerSocket);
        exit(EXIT_FAILURE);
    }

    cout << "Servidor escuchando en el puerto " << port << "..." << endl;

    for (;;)
    {
        int ClientSocket = accept(ServerSocket, NULL, NULL);
        if (0 > ClientSocket)
        {
            perror("error accept failed");
            continue; // no matamos el servidor por un accept fallido
        }
        thread(ThreadReadClient, ClientSocket).detach();
    }

    close(ServerSocket);
    return 0;
}
