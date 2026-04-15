#include "UartRingbuffer_multi.h"
#include "ESP8266_HAL.h"
#include "stdio.h"
#include "string.h"

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

#define wifi_uart &huart1
#define pc_uart &huart2

// Variabili globali per stabilità RAM
char buffer[20];
// Sostituisci queste righe all'inizio di ESP8266_HAL.c
char datatosend[800]; // Ridotto a 800 per risparmiare RAM globale
int termostato_attivo = 0;
int setpoint_decimi = 200;

// HTML più compatto (senza spazi inutili per risparmiare byte)
char *Basic_inclusion = "<!DOCTYPE html><html><head>"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<style>html{font-family:Helvetica;text-align:center;} "
".btn{display:block;width:120px;color:#fff;padding:10px;text-decoration:none;margin:20px auto;border-radius:4px;} "
".on{background:#1abc9c;}.off{background:#34495e;}.temp{font-size:22px;font-weight:bold;color:#e67e22;}</style>"
"</head><body><h1>TERMOSTATO</h1>";

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
    char data[40];

    sprintf (data, "AT+CIPSEND=%d,%d\r\n", Link_ID, len);
    Uart_sendstring(data, wifi_uart);

    while (!(Wait_for(">", wifi_uart)));
    Uart_sendstring (str, wifi_uart);

    while (!(Wait_for("SEND OK", wifi_uart)));

    // MODIFICA QUI: Invia il comando di chiusura ma NON bloccare il sistema con un Wait_for
    sprintf (data, "AT+CIPCLOSE=%d\r\n", Link_ID);
    Uart_sendstring(data, wifi_uart);

    // Un piccolo delay è più sicuro di un Wait_for infinito in questo caso
    HAL_Delay(100);

    return 1;
}

void Server_Handle (char *str, int Link_ID)
{
    char temp_str[150];
    memset(datatosend, 0, sizeof(datatosend));
    strcpy(datatosend, Basic_inclusion);

    if (termostato_attivo == 0)
    {
        // SPEGNIMENTO TOTALE
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0); // LED Scheda
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, 0); // LED Breadboard

        strcat(datatosend, "<p>STATO: <b>OFF</b></p>");
        strcat(datatosend, "<a class=\"btn on\" href=\"/ledon\">ACCENDI</a>");
    }
    else
    {
    	uint32_t adc_val = read_adc();
    	    // Calcolo temperatura in decimi (es: 22.5°C -> 225)
		int temp_attuale = (int)((adc_val * 330.0) / 4095.0);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);
        // Logica Riscaldamento
        if (temp_attuale < setpoint_decimi)
        {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, 1);
            sprintf(temp_str, "<p class=\"temp\">ORA: %d.%d C</p><p style='color:red'>ACCESO</p>", temp_attuale/10, temp_attuale%10);
        }
        else
        {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, 0);
            sprintf(temp_str, "<p class=\"temp\">ORA: %d.%d C</p><p style='color:green'>SPENTO</p>", temp_attuale/10, temp_attuale%10);
        }
        strcat(datatosend, temp_str);

        // Form impostazione
        sprintf(temp_str, "<p>TARGET: %d.%d</p>", setpoint_decimi/10, setpoint_decimi%10);
        strcat(datatosend, temp_str);
        strcat(datatosend, "<form action=\"/set\" method=\"GET\"><input type=\"number\" name=\"t\" style=\"width:60px\"><input type=\"submit\" value=\"SET\"></form>");

        strcat(datatosend, "<a class=\"btn off\" href=\"/ledoff\">SPEGNI</a>");
    }

    strcat(datatosend, "</body></html>");
    Server_Send(datatosend, Link_ID);
}
void Server_Start (void)
{
	char buftocopyinto[128] = {0};
	char Link_ID;
	if (!(Get_after("+IPD,", 1, &Link_ID, wifi_uart))) return;
	Link_ID -= 48;
	while (!(Copy_upto(" HTTP/1.1", buftocopyinto, wifi_uart)));

	if (Look_for("/ledon", buftocopyinto) == 1)
	{
		termostato_attivo = 1;
		Server_Handle("/ledon", Link_ID);
	}
	else if (Look_for("/ledoff", buftocopyinto) == 1)
	{
		termostato_attivo = 0;
		Server_Handle("/ledoff", Link_ID);
	}
	else if (Look_for("/set?t=", buftocopyinto) == 1)
	{
		char *ptr = strstr(buftocopyinto, "t=");
		if (ptr != NULL) sscanf(ptr, "t=%d", &setpoint_decimi);
		termostato_attivo = 1;
		Server_Handle("/set", Link_ID);
	}
	else if (Look_for("/favicon.ico", buftocopyinto) == 1);
	else
	{
		Server_Handle("/", Link_ID);
	}
}
