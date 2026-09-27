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

using namespace std;

map<string, int> ListOfCli;
mutex mtx;

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