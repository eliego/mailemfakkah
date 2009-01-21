/* mailemfakkah.c
** Syslog-ng mailer, by Kung den Knege (kungdenknege@gmail.com)
**
** For those of you who would like syslog-ng to mail alerts to you, and, 
** like me, doesn't get a shit of the mail command, or just wanna use an SMTP-server
** on another host. If your thinking about telling me that there's already like 1000
** identical apps out there - think again, or i'll track you down and bite your head. Enjoy!
**
** Unfortunately, I'm waay to lazy to write a Makefile - compile and install by (as root):
**
** cc -Wall -o mailemfakkah mailemfakkah.c
** mv mailemfakkah /usr/local/sbin/
** chmod 0700 /usr/local/sbin/mailemfakkah
**
** And add this to destinatino of desire (in syslog-ng.conf, dipshit)
**
** program("/usr/local/sbin/mailemfakkah");
*/


#define RECIPIENT "kungdenknege@gmail.com"
#define SUBJECT "Alert from syslog-ng"
#define SMTP "mail1.telia.com"
#define THRESHOLD 30

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <time.h>

void error(char *mesg, int die);
int getconnectedsocket(char *hostname, int port);
char *getseterror(char *mesg);
int parsedata(char *data, char **host, char **prog, char **mesg, int len);
int parsedomain(char *hostname, char **domain, int len);
int recvcode(int sock);
int sendcmd(int sock, char *cmd);

int main() {
	char *host;
	char *prog;
	char *mesg;
	char buf[600];
	char temp[600];
	char hostname[60];
	char *domain;
	int sock, i;

	// Get hostname
	gethostname(hostname, 60);
	if (parsedomain(hostname, &domain, 60) == -1) {
		error("Couldn't get domain - using hotmail.com. Take that, M$!", 0);
		strcpy(hostname, "hotmail.com");
		domain = hostname;
	}

	// Read line from stdin
	while (fgets(buf, 600, stdin) != NULL) {
		if (parsedata(buf, &host, &prog, &mesg, 600) == -1) {
			error("Alert came but couldn't be parsed", 0);
			continue;
		}

		// Open socket
		if ((sock = getconnectedsocket(SMTP, 25)) == -1)
			error(NULL, 1);

		// Read banner
		getseterror("Server threw us out, or syntax error");
		if (recvcode(sock) != 220)
			 error(NULL, 1);
		
		// HELO
		snprintf(temp, 600, "HELO %s", hostname);
		sendcmd(sock, temp);
		if (recvcode(sock) != 250)
			error(NULL, 1);
	
		// MAIL
		snprintf(temp, 600, "MAIL FROM: <%s@%s>", hostname, domain);
		sendcmd(sock, temp);
		if (recvcode(sock) != 250)
			error(NULL, 1);

		// RCPT
		snprintf(temp, 600, "RCPT TO: <%s>", RECIPIENT);
		sendcmd(sock, temp);
		if ((i = recvcode(sock)) != 250 && i != 251)
			error(NULL, 1);

		// DATA
		sendcmd(sock, "DATA");
		if (recvcode(sock) != 354)
			error(NULL, 1);

		// Mail
		snprintf(temp, 600, "Subject: %s\r\nFrom: <%s@%s>\r\nTo: <%s>\r\n", SUBJECT, hostname, domain, RECIPIENT);
		sendcmd(sock, temp);
		snprintf(temp, 600, "At %d %s on %s alerts:\r\n%s\r\n.", time(NULL), prog, host, mesg);
		sendcmd(sock, temp);
		if (recvcode(sock) != 250)
			error(NULL, 1);

		// QUIT
		sendcmd(sock, "QUIT");
		if (recvcode(sock) != 221)
			error("Mail sent, but server didn't let us QUIT", 0);

		// Close
		close(sock);

		// Threshold
		sleep(THRESHOLD);
	}

	closelog();
	exit(0);
}

void error(char *mesg, int die) {
	mesg = getseterror(mesg);
	openlog("mailemfakkah", 0, LOG_SYSLOG);
	syslog(LOG_ERR, mesg);
	closelog();
	
	if (die != 0)
		exit(die);

	return;
}

int getconnectedsocket(char *hostname, int port) {
	struct hostent *hostdata;
	struct sockaddr_in host;
	int sock;

	if ((hostdata = gethostbyname(hostname)) == NULL) {
		getseterror("Could not resolve hostname");
		return -1;
	}

	host.sin_family = AF_INET;
	host.sin_port = htons(port);
	host.sin_addr = *((struct in_addr *)hostdata->h_addr);
	memset(&(host.sin_zero), '\0', 8);
	
	if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
		getseterror("Could not create socket");
		return -1;
	}
	
	if (connect(sock, (struct sockaddr *)&host, sizeof(struct sockaddr)) == -1) {
		getseterror("Couldn't connect to server!");
		return -1;
	}

	// Woho! We did it! C6 - here i come!!!
	return sock;
}

char *getseterror(char *mesg) {
	static char *message;

	if (mesg == NULL && message != NULL) 
		return message;
	if (mesg == NULL && message == NULL)
		return "Undetermined error";
	if (mesg != NULL)
		message = mesg;

	return message;
}

int parsedata(char *data, char **host, char **prog, char **mesg, int len) {
	if (strlen(data) > 22) {
		data[strlen(data) - 1] = '\0';
		// Parse host, prog and message
		if ((*host = strtok(data + 20, " ")) == NULL)
			return -1;

		if ((*prog = strtok(NULL, ":")) == NULL)
			return -1;
		
		if (strlen(*host) + strlen(*prog) + 3 >= len)
			return -1;

		*mesg = (char *)(*prog + strlen(*prog) + 2);

		return 0;
	} else
		return -1;
}

int recvcode(int sock) {
	char code[3];
	int res, i;

	if ((res = recv(sock, code, 3, 0)) <= 0)
		return res;
	code[3] = '\0';

	do {
		while (i != '\r') 
			if ((res = recv(sock, &i, 1, 0)) <= 0)
				return res;

		if ((res = recv(sock, &i, 1, 0)) <= 0)
			return res;
	} while (i != '\n');

	// Convert to int
	res = 0;
	for (i = 0; i < strlen(code); i++) {
		if (isdigit(code[i]) == 0)
			return -1;
		res *=10;
		res += code[i] - '0';
	}

	return res;
}

int sendcmd(int sock, char *cmd) {
	char *command = (char *)malloc((strlen(cmd) + 2) * sizeof(char));
	int sent = 0;
	int temp = 0;

	strcpy(command, cmd);
	strcat(command, "\r\n");

	while (sent < strlen(command)) {
		if ((temp = send(sock, command + sent, strlen(command + sent), 0)) == -1) {
			free(command);
			return -1;
		}
		sent += temp;
	}
	
	free(command);
	return 0;
}

int parsedomain(char *hostname, char **domain, int len) {
	if ((*domain = strtok(hostname, ".")) == NULL)
		return -1;

	if (strlen(*domain) + 1 >= len)
		return -1;

	*domain = hostname + strlen(*domain) + 1;

	return 0;
}