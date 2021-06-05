#include <doh_client.h>
#include <http.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h> //printf
#include <unistd.h>

#define HOST "208.67.222.222:53"

void change_to_dns_format(char* dns, char* host);
unsigned char * getBody(unsigned char * buf);
unsigned char * getName(char * buf, unsigned char * body);
char * create_post(int length, char * body);

char * server_ip = "127.0.0.1";
int id = 0;

//DNS header structure
struct DNS_HEADER {
    unsigned short id; // identification number

    unsigned char rd :1; // recursion desired
    unsigned char tc :1; // truncated message
    unsigned char aa :1; // authoritive answer
    unsigned char opcode :4; // purpose of message
    unsigned char qr :1; // query/response flag

    unsigned char rcode :4; // response code
    unsigned char cd :1; // checking disabled
    unsigned char ad :1; // authenticated data
    unsigned char z :1; // its z! reserved
    unsigned char ra :1; // recursion available

    uint16_t q_count; // number of question entries
    uint16_t ans_count; // number of answer entries
    uint16_t auth_count; // number of authority entries
    uint16_t add_count; // number of resource entries
};

//Constant sized fields of query structure
struct QUESTION {
    uint16_t qtype;
    uint16_t qclass;
};

//Constant sized fields of the resource record structure
struct R_DATA {
    uint16_t type;
    uint16_t _class;
    uint32_t ttl;
    uint16_t data_len;
};

//Pointers to resource record contents
struct RES_RECORD {
    unsigned char *name;
    struct R_DATA *resource;
    unsigned char *rdata;
};

//Structure of a Query
typedef struct {
    unsigned char *name;
    struct QUESTION *ques;
} QUERY;

void fillStruct() {
    unsigned char buf[65536],*qname,*reader;

    struct DNS_HEADER *dns;
    struct QUESTION *qinfo = NULL;

    dns = (struct DNS_HEADER *)&buf;
    dns->id = id;
    dns->qr = 0; //This is a query
    dns->opcode = 0; //This is a standard query
    dns->aa = 0; //Not Authoritative
    dns->tc = 0; //This message is not truncated
    dns->rd = 1; //Recursion Desired
    dns->ra = 0; //Recursion not available! hey we dont have it (lol)
    dns->z = 0;
    dns->ad = 0;
    dns->cd = 0;
    dns->rcode = 0;
    dns->q_count = htons(1); //we have only 1 question
    dns->ans_count = 0;
    dns->auth_count = 0;
    dns->add_count = 0;

    qname = (unsigned char*)&buf[sizeof(struct DNS_HEADER)];

    char * host = "www.google.com"; // esto seria variable
    change_to_dns_format((char *)qname, host); // mete el host en la posición del qname

    qinfo =(struct QUESTION*)&buf[sizeof(struct DNS_HEADER) + (strlen((const char*)qname) + 1)]; //fill it
    qinfo->qtype = htons(255); //type of the query , A , MX , CNAME , NS etc
    qinfo->qclass = htons(1); //its internet (lol)

    char * string = create_post((int)(sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION)), (char *)buf); // crea el http request
    // printf("%s", string);

    /* esta parte es porque el body no se esta creando adecuadamente en el factory */
    char * aux = malloc(200);
    memcpy(aux, string, strlen(string));
    memcpy(aux + strlen(string), buf, sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION));
    aux[strlen(string) + sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION)] = '\0';

    printf("Resolving %s", host);

    struct sockaddr_in dest;
    int s = socket(AF_INET , SOCK_STREAM , IPPROTO_TCP); //TCP packet for DNS queries

    dest.sin_family = AF_INET;
    dest.sin_port = htons(8053); // el servidor DOH esta en localhost:8053
    dest.sin_addr.s_addr = inet_addr(server_ip); //dns servers, esto deberia ser variable al igual que el port

    if (connect(s, (struct sockaddr*)&dest,sizeof(dest)) < 0) { // establece la conexión con el servidor DOH
        perror("error in connect");
    }

    printf("\nSending Packet...");
    if( send(s,(char*)aux, strlen(string) + sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION),0) < 0) { // manda el paquete al servidor DOH
        perror("sendto failed");
    }
    free(string); // libera el request http

    int i = sizeof dest;
    printf("\nReceiving answer...");
    if(recvfrom (s,(unsigned char*)buf , 65536 , 0 , (struct sockaddr*)&dest , (socklen_t*)&i ) < 0) { // se recive el response HTTP
        perror("recvfrom failed");
    }


    /* Comienza la lectura del response HTTP */
    unsigned char * body = getBody(buf); // me muevo directo al body
    dns = (struct DNS_HEADER*) body; // obtengo el DNS_HEADER
    printf("\nThe response contains : ");
    printf("\n %d Questions.",ntohs(dns->q_count));
    printf("\n %d Answers.",ntohs(dns->ans_count));
    printf("\n %d Authoritative Servers.",ntohs(dns->auth_count));
    printf("\n %d Additional records.\n\n",ntohs(dns->add_count));

    char name[20];
    unsigned char * dataDir = getName(name, body + (int)(sizeof(struct DNS_HEADER))); // obtengo el nombre del question, reemplazo los .
    printf("%s", name);

    struct QUESTION * question = (struct QUESTION *) (dataDir + 1); // quiero los atributos de la answer
    printf("\n %d Type.", ntohs(question->qtype));
    printf("\n %d Class.", ntohs(question->qclass));

    reader = dataDir + 1 + sizeof(struct QUESTION);
    struct R_DATA * answer = (struct R_DATA *) (getName(name, reader) + 1); // quiero los atributos de la answer
    printf("\n%s\n", name);

    printf("\nThe response contains : ");
    printf("\n %d Type.",ntohs(answer->type));
    printf("\n %d Class.",ntohs(answer->_class));
    printf("\n %d TTL.", (unsigned int)answer->ttl);
    printf("\n %d Length.\n\n",ntohs(answer->data_len));

    struct sockaddr_in a;
    a.sin_addr.s_addr=(*(long *)(answer + sizeof(struct R_DATA))); //working without ntohl
    printf("has IPv4 address : %s",inet_ntoa(a.sin_addr));
}

void change_to_dns_format(char* dns, char* host) {
    int len = (int)strlen(host);
    memcpy(dns, ".", 1);
    memcpy(dns + 1, host, len);

    char cant = 0;
    for (int i = (int)strlen(dns) - 1; i >= 0; i--) {
        if (dns[i] != '.') {
            cant++;
        } else {
            dns[i] = cant;
            cant = 0;
        }
    }
    dns[len + 1] = 0;
}

unsigned char * getBody(unsigned char * buf) {
    int i = 0;
    int done = 1;
    int flag = 0;
    putchar('\n');
    while (done) {
        if (buf[i+1] == '\n' && buf[i] == '\r') {
            if (flag) done = 0;
            else flag = 1;
            i++;
        } else {
            flag = 0;
        }
        i++;
    }
    return buf + i;
}

unsigned char * getName(char * buf, unsigned char * body) {
    char c;
    int pos = 0;
    body++; // para evitar el primer punto
    while ((c = *((char *)body)) != 0) {
        if (c < 'a') buf[pos++] = '.';
        else buf[pos++] = c;
        body++;
    }
    buf[pos] = '\0';
    return body;
}

char * create_post(int length, char * body) {
    int header_count = 4;
    char *** headers = malloc(sizeof(char *)*HEADER_COLUMNS*header_count);
    char * header1[2] = {"Accept", "application/dns-message"};
    char * header2[2] = {"Content-Type", "application/dns-message"};
    char * header3[2] = {"Content-Length", 0};
    header3[1] = malloc(4);
    sprintf(header3[1], "%d", length);
    char * header4[2] = {"Host", HOST};
    headers[0] = header1;
    headers[1] = header2;
    headers[2] = header3;
    headers[3] = header4;
    struct request request = {
            .method = POST,
            .body = body,
            .headers = headers,
            .header_count = header_count,
            .file = "getnsrecord"
    };
    free(headers);

    return create_request(&request);
}












