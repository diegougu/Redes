/* Client code in C++ - Interactive Chat Client */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <thread>
#include <sstream>
#include <vector>
#include <fstream>
#include <algorithm>

using namespace std;

int SocketFD;
bool running = true;

/* -------------------------------------------------------------------- */
/* Devuelve solo el nombre de archivo de una ruta (sin directorios)     */
/* -------------------------------------------------------------------- */
string getBaseName(const string &path)
{
    size_t pos = path.find_last_of("/\\");
    if (pos == string::npos)
        return path;
    return path.substr(pos + 1);
}

/* -------------------------------------------------------------------- */
/* Convierte un numero a string con ceros a la izquierda hasta 'size'   */
/* -------------------------------------------------------------------- */
string zeroPad(long long number, int size)
{
    string str = to_string(number);

    if ((int)str.length() >= size)
        return str;

    return string(size - str.length(), '0') + str;
}

/* -------------------------------------------------------------------- */
/* Lee exactamente 'len' bytes del socket S hacia buff                  */
/* -------------------------------------------------------------------- */
long readExact(int S, char *buff, long len)
{
    long totalRead = 0;
    while (totalRead < len)
    {
        long n = read(S, buff + totalRead, len - totalRead);
        if (n <= 0)
            return n;
        totalRead += n;
    }
    return totalRead;
}

/* -------------------------------------------------------------------- */
/* Thread que escucha continuamente al servidor y muestra lo que llega  */
/* -------------------------------------------------------------------- */
void ThreadReadServer(int S)
{
    char buff[1024];
    int n, tamano;
    string remitente, msg;

    while (running)
    {
        n = readExact(S, buff, 1);
        if (n <= 0)
        {
            cout << "\n[!] Se perdio la conexion con el servidor." << endl;
            running = false;
            break;
        }

        if (buff[0] == 'm' || buff[0] == 'b')
        {
            char accion = buff[0];

            n = readExact(S, buff, 7);
            if (n <= 0) break;
            buff[n] = '\0';
            tamano = atoi(buff);

            n = readExact(S, buff, tamano);
            if (n <= 0) break;
            buff[n] = '\0';
            remitente = buff;

            n = readExact(S, buff, 11);
            if (n <= 0) break;
            buff[n] = '\0';
            tamano = atoi(buff);

            n = readExact(S, buff, tamano);
            if (n <= 0) break;
            buff[n] = '\0';
            msg = buff;

            cout << "\n[TRAMA RECIBIDA] " << accion
                 << " | " << zeroPad(remitente.size(), 7) << " | " << remitente
                 << " | " << zeroPad(msg.size(), 11) << " | " << msg << endl;

            if (accion == 'm')
                cout << "[Privado de " << remitente << "]: " << msg << "\n> " << flush;
            else
                cout << "[Broadcast de " << remitente << "]: " << msg << "\n> " << flush;
        }
        else if (buff[0] == 'F')
        {
            /* Trama Ser->Cli: F | size13 origen | origen | size13 nombreArchivo | nombreArchivo | size25 tamano | archivo */
            n = readExact(S, buff, 13);
            if (n <= 0) break;
            buff[n] = '\0';
            int origenSize = atoi(buff);

            string origen(origenSize, '\0');
            if (origenSize > 0)
            {
                n = readExact(S, &origen[0], origenSize);
                if (n <= 0) break;
            }

            n = readExact(S, buff, 13);
            if (n <= 0) break;
            buff[n] = '\0';
            int filenameSize = atoi(buff);

            string nombreArchivo(filenameSize, '\0');
            if (filenameSize > 0)
            {
                n = readExact(S, &nombreArchivo[0], filenameSize);
                if (n <= 0) break;
            }

            n = readExact(S, buff, 25);
            if (n <= 0) break;
            buff[n] = '\0';
            long long fileSize = atoll(buff);

            cout << "\n[TRAMA RECIBIDA] F | " << zeroPad(origen.size(), 13) << " | " << origen
                 << " | " << zeroPad(nombreArchivo.size(), 13) << " | " << nombreArchivo
                 << " | " << zeroPad(fileSize, 25) << " | (" << fileSize << " bytes)" << endl;

            /* El archivo se escribe a disco en chunks a medida que llega, nunca se
               junta completo en memoria: asi soporta archivos de varios GB. */
            string outName = "recibido_" + origen + "_" + nombreArchivo;
            ofstream outFile(outName, ios::binary);

            const size_t CHUNK = 1 << 20; // 1 MB
            vector<char> chunkBuf(CHUNK);
            long long remaining = fileSize;
            bool ok = outFile.good();

            while (remaining > 0)
            {
                long toRead = (long)min((long long)CHUNK, remaining);
                long got = readExact(S, chunkBuf.data(), toRead);
                if (got <= 0) { ok = false; break; }
                if (outFile) outFile.write(chunkBuf.data(), got);
                remaining -= got;
            }

            if (remaining > 0)
            {
                cout << "[!] Conexion perdida a mitad de la recepcion de " << nombreArchivo << "\n> " << flush;
                outFile.close();
                break;
            }

            outFile.close();
            if (ok && outFile)
                cout << "[Archivo recibido de " << origen << "]: " << nombreArchivo
                     << " (" << fileSize << " bytes) guardado como \"" << outName << "\"\n> " << flush;
            else
                cout << "[!] No se pudo guardar el archivo recibido de " << origen
                     << " (" << nombreArchivo << ")\n> " << flush;
        }
        else if (buff[0] == 'L')
        {
            /* La respuesta L contiene: L + tamano del CSV + CSV. */
            n = readExact(S, buff, 11);
            if (n <= 0) break;
            buff[n] = '\0';
            tamano = atoi(buff);

            /* Se reserva exactamente el espacio necesario para el CSV recibido. */
            vector<char> csv(tamano);
            if (tamano > 0)
            {
                n = readExact(S, csv.data(), tamano);
                if (n <= 0) break;
            }

            string lista(csv.begin(), csv.end());
            cout << "\n[TRAMA RECIBIDA] L | " << zeroPad(tamano, 11)
                 << " | " << lista << endl;
            cout << "[Usuarios conectados]: " << lista << "\n> " << flush;
        }
        else if (buff[0] == 'E')
        {
            n = readExact(S, buff, 11);
            if (n <= 0) break;
            buff[n] = '\0';
            tamano = atoi(buff);

            n = readExact(S, buff, tamano);
            if (n <= 0) break;
            buff[n] = '\0';
            msg = buff;

            cout << "\n[TRAMA RECIBIDA] E | " << zeroPad(msg.size(), 11)
                 << " | " << msg << endl;
            cout << "[Error] El usuario no esta conectado. Mensaje no enviado: "
                 << msg << "\n> " << flush;
        }
        
        /* otros bytes de accion desconocidos se ignoran */
    }
}

int main(int argc, char *argv[])
{
    const char *serverIP = "127.0.0.1";
    int port = 45000;

    if (argc >= 2) serverIP = argv[1];
    if (argc >= 3)
    {
        int p = atoi(argv[2]);
        if (p > 0) port = p;
    }

    struct sockaddr_in stSockAddr;
    int Res;
    SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (-1 == SocketFD)
    {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(port);
    Res = inet_pton(AF_INET, serverIP, &stSockAddr.sin_addr);

    if (0 > Res)
    {
        perror("error: first parameter is not a valid address family");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    else if (0 == Res)
    {
        perror("char string (second parameter) does not contain a valid IP address");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if (-1 == connect(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in)))
    {
        perror("connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    cout << "Conectado al servidor " << serverIP << ":" << port << endl;

    /* --- Pedir nickname y enviar frame de registro 'N' --- */
    string nickname;
    cout << "Ingresa tu nickname: ";
    getline(cin, nickname);

    string frame;
    frame += 'N';
    frame += zeroPad(nickname.size(), 7);
    frame += nickname;

    cout << "[TRAMA ENVIADA] N | " << zeroPad(nickname.size(), 7) << " | " << nickname << endl;
    write(SocketFD, frame.c_str(), frame.size());

    /* --- Lanzar thread que escucha al servidor --- */
    thread listenerThread(ThreadReadServer, SocketFD);

    cout << "Comandos disponibles:" << endl;
    cout << "  /m <nick> <mensaje>   -> mensaje privado (unicast)" << endl;
    cout << "  /b <mensaje>          -> mensaje a todos (broadcast)" << endl;
    cout << "  /f <nick> <ruta>      -> enviar archivo a un usuario" << endl;
    cout << "  lista                 -> listar usuarios conectados" << endl;
    cout << "  /q                    -> salir" << endl;

    string line;
    while (running)
    {
        cout << "> " << flush;
        if (!getline(cin, line))
            break;

        if (line.empty())
            continue;

        if (line.substr(0, 2) == "/m")
        {
            /* formato: /m destino mensaje... */
            istringstream iss(line.substr(3));
            string destino;
            iss >> destino;
            string mensaje;
            getline(iss, mensaje);
            if (!mensaje.empty() && mensaje[0] == ' ')
                mensaje.erase(0, 1);

            frame.clear();
            frame += 'M';
            frame += zeroPad(destino.size(), 7);
            frame += destino;
            frame += zeroPad(mensaje.size(), 11);
            frame += mensaje;

            cout << "[TRAMA ENVIADA] M | " << zeroPad(destino.size(), 7) << " | " << destino
                 << " | " << zeroPad(mensaje.size(), 11) << " | " << mensaje << endl;
            write(SocketFD, frame.c_str(), frame.size());
        }
        else if (line.substr(0, 2) == "/b")
        {
            string mensaje = line.size() > 3 ? line.substr(3) : "";

            frame.clear();
            frame += 'B';
            frame += zeroPad(mensaje.size(), 11);
            frame += mensaje;

            cout << "[TRAMA ENVIADA] B | " << zeroPad(mensaje.size(), 11) << " | " << mensaje << endl;
            write(SocketFD, frame.c_str(), frame.size());
        }
        else if (line.substr(0, 2) == "/f")
        {
            /* formato: /f destino ruta_del_archivo */
            istringstream iss(line.substr(3));
            string destino;
            iss >> destino;
            string filepath;
            getline(iss, filepath);
            if (!filepath.empty() && filepath[0] == ' ')
                filepath.erase(0, 1);

            if (destino.empty() || filepath.empty())
            {
                cout << "Uso: /f <nickname_destino> <ruta_del_archivo>" << endl;
            }
            else
            {
                /* Se abre en modo binario para no corromper caracteres ASCII especiales. */
                ifstream fileStream(filepath, ios::binary | ios::ate);
                if (!fileStream)
                {
                    cout << "[!] No se pudo abrir el archivo: " << filepath << endl;
                }
                else
                {
                    long long fileSize = (long long)fileStream.tellg();
                    fileStream.seekg(0, ios::beg);

                    string nombreArchivo = getBaseName(filepath);

                    /* Se envia primero el encabezado, y luego el archivo en chunks
                       leidos directamente del disco: nunca se carga completo a RAM,
                       para poder enviar archivos de varios GB sin problema. */
                    frame.clear();
                    frame += 'F';
                    frame += zeroPad((long long)destino.size(), 13);
                    frame += destino;
                    frame += zeroPad((long long)nombreArchivo.size(), 13);
                    frame += nombreArchivo;
                    frame += zeroPad(fileSize, 25);
                    write(SocketFD, frame.data(), frame.size());

                    cout << "[TRAMA ENVIADA] F | " << zeroPad((long long)destino.size(), 13) << " | " << destino
                         << " | " << zeroPad((long long)nombreArchivo.size(), 13) << " | " << nombreArchivo
                         << " | " << zeroPad(fileSize, 25) << " | (" << fileSize << " bytes)" << endl;

                    const size_t CHUNK = 1 << 20; // 1 MB
                    vector<char> chunkBuf(CHUNK);
                    long long remaining = fileSize;
                    bool sendOk = true;

                    while (remaining > 0)
                    {
                        streamsize toRead = (streamsize)min((long long)CHUNK, remaining);
                        if (!fileStream.read(chunkBuf.data(), toRead))
                        {
                            sendOk = false;
                            break;
                        }
                        long written = 0;
                        while (written < toRead)
                        {
                            ssize_t w = write(SocketFD, chunkBuf.data() + written, toRead - written);
                            if (w <= 0) { sendOk = false; break; }
                            written += w;
                        }
                        if (!sendOk) break;
                        remaining -= toRead;
                    }

                    if (!sendOk)
                        cout << "[!] Error enviando el archivo: " << filepath << endl;
                    else
                        cout << "[Archivo enviado a " << destino << "]: " << nombreArchivo << endl;
                }
            }
        }
        else if (line.substr(0, 2) == "/q")
        {
            frame.clear();
            frame += 'Q';

            cout << "[TRAMA ENVIADA] Q" << endl;
            write(SocketFD, frame.c_str(), frame.size());
            running = false;
        }
        else if (line == "lista")
        {
            /* La solicitud de lista solo necesita enviar la accion L. */
            frame.clear();
            frame += 'L';

            cout << "[TRAMA ENVIADA] L" << endl;
            write(SocketFD, frame.c_str(), frame.size());
        }
        else
        {
            cout << "Comando no reconocido. Usa /m, /b, lista o /q." << endl;
        }
    }

    shutdown(SocketFD, SHUT_RDWR);
    close(SocketFD);

    if (listenerThread.joinable())
        listenerThread.join();

    return 0;
}