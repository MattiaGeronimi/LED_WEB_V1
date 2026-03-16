/*
 * ESP8266_HAL.c
 *
 *  Created on: Apr 14, 2020
 *      Author: Controllerstech
 */


#include "UartRingbuffer_multi.h"
#include "ESP8266_HAL.h"
#include "stdio.h"
#include "string.h"

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

#define wifi_uart &huart1
#define pc_uart &huart2


char buffer[20];


char *Basic_inclusion = "<!DOCTYPE html> <html>\n\
<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n\
<title>TERMOSTATO WEB CONTROL</title>\n\
<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n\
body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n\
h4 {color: #888; margin-top: -20px;}\n\
.button {display: block; width: 80px; background-color: #1abc9c; border: none; color: white;\n\
padding: 13px 30px; text-decoration: none; font-size: 25px; margin: 20px auto; cursor: pointer; border-radius: 4px;}\n\
.button-on {background-color: #1abc9c;} .button-off {background-color: #34495e;}\n\
p {font-size: 18px; color: #444; margin-bottom: 10px;}\n\
.temp {font-size: 24px; font-weight: bold; color: #e67e22;}\n\
</style></head>\n\
<body>\n\
<h1>TERMOSTATO WEB</h1>\n\
<h4>Di: AGG</h4>";

// Queste stringhe verranno composte dinamicamente nella funzione Server_Handle
char *Terminate = "</body></html>";



/*****************************************************************************************************************************************/

void ESP_Init (char *SSID, char *PASSWD)
{
	char data[80];

	Ringbuf_init();

	Uart_sendstring("AT+RST\r\n", wifi_uart);
	Uart_sendstring("RESETTING.", pc_uart);
	for (int i=0; i<5; i++)
	{
		Uart_sendstring(".", pc_uart);
		HAL_Delay(1000);
	}

	/********* AT **********/
	Uart_sendstring("AT\r\n", wifi_uart);
	while(!(Wait_for("AT\r\r\n\r\nOK\r\n", wifi_uart)));
	Uart_sendstring("AT---->OK\n\n", pc_uart);


	/********* AT+CWMODE=1 **********/
	Uart_sendstring("AT+CWMODE=1\r\n", wifi_uart);
	while (!(Wait_for("AT+CWMODE=1\r\r\n\r\nOK\r\n", wifi_uart)));
	Uart_sendstring("CW MODE---->1\n\n", pc_uart);


	/********* AT+CWJAP="SSID","PASSWD" **********/
	Uart_sendstring("connecting... to the provided AP\n", pc_uart);
	sprintf (data, "AT+CWJAP=\"%s\",\"%s\"\r\n", SSID, PASSWD);
	Uart_sendstring(data, wifi_uart);
	while (!(Wait_for("WIFI GOT IP\r\n\r\nOK\r\n", wifi_uart)));
	sprintf (data, "Connected to,\"%s\"\n\n", SSID);
	Uart_sendstring(data,pc_uart);


	/********* AT+CIFSR **********/
	Uart_sendstring("AT+CIFSR\r\n", wifi_uart);
	while (!(Wait_for("CIFSR:STAIP,\"", wifi_uart)));
	while (!(Copy_upto("\"",buffer, wifi_uart)));
	while (!(Wait_for("OK\r\n", wifi_uart)));
	int len = strlen (buffer);
	buffer[len-1] = '\0';
	sprintf (data, "IP ADDR: %s\n\n", buffer);
	Uart_sendstring(data, pc_uart);


	Uart_sendstring("AT+CIPMUX=1\r\n", wifi_uart);
	while (!(Wait_for("AT+CIPMUX=1\r\r\n\r\nOK\r\n", wifi_uart)));
	Uart_sendstring("CIPMUX---->OK\n\n", pc_uart);

	Uart_sendstring("AT+CIPSERVER=1,80\r\n", wifi_uart);
	while (!(Wait_for("OK\r\n", wifi_uart)));
	Uart_sendstring("CIPSERVER---->OK\n\n", pc_uart);

	Uart_sendstring("Now Connect to the IP ADRESS\n\n", pc_uart);

}




int Server_Send (char *str, int Link_ID)
{
	int len = strlen (str);
	char data[80];
	sprintf (data, "AT+CIPSEND=%d,%d\r\n", Link_ID, len);
	Uart_sendstring(data, wifi_uart);
	while (!(Wait_for(">", wifi_uart)));
	Uart_sendstring (str, wifi_uart);
	while (!(Wait_for("SEND OK", wifi_uart)));
	sprintf (data, "AT+CIPCLOSE=5\r\n");
	Uart_sendstring(data, wifi_uart);
	while (!(Wait_for("OK\r\n", wifi_uart)));
	return 1;
}

void Server_Handle (char *str, int Link_ID)
{
    char datatosend[1024] = {0};
    char temp_str[100];

    // Inizia con l'intestazione
    strcpy(datatosend, Basic_inclusion);

    // Aggiunge la temperatura corrente al corpo della pagina
    // Versione senza bisogno del supporto float
    float temperatura_attuale = 22.5;
    int intero = (int)temperatura_attuale;
    int decimale = (int)((temperatura_attuale - intero) * 10); // Prende la prima cifra decimale

    sprintf(temp_str, "<p>Temperatura Corrente: <span class=\"temp\">%d.%d C</span></p>", intero, decimale);
    strcat(datatosend, temp_str);

    // Gestisce lo stato del LED e il bottone
    if (!(strcmp (str, "/ledon")))
    {
        strcat(datatosend, "<p>Stato TERMOSTATO: <b>ON</b></p>");
        strcat(datatosend, "<a class=\"button button-off\" href=\"/ledoff\">OFF</a>");
    }
    else
    {
        strcat(datatosend, "<p>Stato TERMOSTATO: <b>OFF</b></p>");
        strcat(datatosend, "<a class=\"button button-on\" href=\"/ledon\">ON</a>");
    }

    strcat(datatosend, Terminate);
    Server_Send(datatosend, Link_ID);
}

void Server_Start (void)
{
	char buftocopyinto[64] = {0};
	char Link_ID;
	while (!(Get_after("+IPD,", 1, &Link_ID, wifi_uart)));
	Link_ID -= 48;
	while (!(Copy_upto(" HTTP/1.1", buftocopyinto, wifi_uart)));
	if (Look_for("/ledon", buftocopyinto) == 1)
	{
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);
		Server_Handle("/ledon",Link_ID);
	}

	else if (Look_for("/ledoff", buftocopyinto) == 1)
	{
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);
		Server_Handle("/ledoff",Link_ID);
	}

	else if (Look_for("/favicon.ico", buftocopyinto) == 1);

	else
	{
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);
		Server_Handle("/ ", Link_ID);
	}
}
