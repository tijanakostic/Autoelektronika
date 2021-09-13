/* Standard includes. */
#include <stdio.h>
#include <conio.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "extint.h"

/* Hardware simulator utility functions */
#include "HW_access.h"

/* SERIAL SIMULATOR CHANNEL TO USE */
#define COM_CH0 (0)
#define COM_CH1 (1)

/* TASK PRIORITIES */
#define	prijem_kanal0 ( tskIDLE_PRIORITY + (UBaseType_t)6) //SerialReceive0
#define ledovke (tskIDLE_PRIORITY + (UBaseType_t)5) //LED_bar_Task
#define	prijem_kanal1 (tskIDLE_PRIORITY + (UBaseType_t)4 ) //SerialReceive1
#define obrada_rezultata (tskIDLE_PRIORITY + (UBaseType_t)3 ) //obrada_senzora
#define	slanje_kanal0 (tskIDLE_PRIORITY + (UBaseType_t)2 ) //SerialSend
#define	display	( tskIDLE_PRIORITY + (UBaseType_t)1 ) //Display

typedef float my_float;

/* TASKS: FORWARD DECLARATIONS */
static void SerialSend0_Task(void* pvParameters);
static void SerialReceive0_Task(void* pvParameters);
static void SerialReceive1_Task(void* pvParameters);
static void LED_bar_Task(void* pvParameters);
static void Obrada_Senzora(void* pvParameters);
static void Display_Task(void* pvParameters);
static void TimerCallBack(TimerHandle_t timer);

void main_demo(void);

static uint8_t  L_P, D_P, L_Z, D_Z, rezim_rada = 0, taster_display = 0;
static uint16_t Vmax = 120, Vtrenutna, Vmax_display = 0;
static my_float Vsrednja = (my_float)0;
static uint8_t  r_point;
static uint8_t NIVO, broj = 0, auto_man = 0;
static uint8_t len, asinhron = 0;

/* 7-SEG NUMBER DATABASE - ALL HEX DIGITS */ //0 do 9
static const uint8_t hex[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
								0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71 };

/* GLOBAL OS-HANDLES */ 
static SemaphoreHandle_t LED_INT_BinarySemaphore1;
static SemaphoreHandle_t Display_BinarySemaphore;
static SemaphoreHandle_t TXC_BinarySemaphore;
static SemaphoreHandle_t RXC_BinarySemaphore0;
static SemaphoreHandle_t RXC_BinarySemaphore1;
static SemaphoreHandle_t Send_BinarySemaphore;

static TimerHandle_t timer1;

static QueueHandle_t serial_queue;



/* INTERRAPT*/
static uint32_t prvProcessRXCInterrupt(void)
{
	BaseType_t xHigherPTW = pdFALSE;

	if (get_RXC_status(0) != 0) {   //interrupt sa kanala 0
		if (xSemaphoreGiveFromISR(RXC_BinarySemaphore0, &xHigherPTW) != pdTRUE) {
			printf("Greskaisr0 \n");
		}
	}
	if (get_RXC_status(1) != 0) {   //interrupt sa kanala 1

		if (xSemaphoreGiveFromISR(RXC_BinarySemaphore1, &xHigherPTW) != pdTRUE) {
			printf("Greskaisr1 \n");
		}
	}
	portYIELD_FROM_ISR((uint32_t)xHigherPTW);  //hocemo da se vratimo u task u kom smo bili prije interapta
}




/* RECEIVE0*/
static void SerialReceive0_Task(void* pvParameters)  //ono sto stize sa kanala0
{
	uint8_t cc;
	static char tmp_str[200], string_queue[200];
	static uint8_t k;
	static uint8_t z = 0;
	for (;;)
	{
		if (xSemaphoreTake(RXC_BinarySemaphore0, portMAX_DELAY) != pdTRUE) {   //predavanje ce biti na svakih 200ms
			printf("Greska take\n");
		}
		if (get_serial_character(COM_CH0, &cc) != 0) {   //karaktere smjestamo u cc
			printf("Greskaget1 \n");
		}
		
		if (cc != (uint8_t)43) {       //0 1 1 0 115+       43-terminator
			tmp_str[z] = (char)cc;     //smjestamo u niz sve karaktere iz cc
			z++;
		}
		else {
			tmp_str[z] = '\0';    //na mjestu plusa stavimo terminator
			z = 0;
			printf("String sa serijske, nulti %s, %c \n", tmp_str, tmp_str[0]);  //ispisujemo string sa serijske
			len = (uint8_t)strlen(tmp_str) % (uint8_t)12;   //ogranicenje da ne predje preko 12 karaktera
			for (k = 0; k < len; k++) {
				string_queue[k] = tmp_str[k]; 
				tmp_str[k] = "";  //praznjenje niza
			}
			string_queue[len] = '\0';
			printf("String **************** %s, %d \n", string_queue, len);
			if (xQueueSend(serial_queue, &string_queue, 0) != pdTRUE) {    //smjestamo u red sve sa kanala0
				printf("Greskared, slanje\n");
			}
			printf("Red za task 3 \n");
		}
	}
}




/*INTERRAPT ZA LEDOVKE*/
static uint32_t OnLED_ChangeInterrupt()  //klik na ledovku predstavlja interrapt
{
	BaseType_t higherPriorityTaskWoken = pdFALSE;
	printf("usao u onledchange\n");
	if (xSemaphoreGiveFromISR(LED_INT_BinarySemaphore1, &higherPriorityTaskWoken) != pdTRUE) {
		printf("Greska \n");
	}
	portYIELD_FROM_ISR((uint32_t)higherPriorityTaskWoken);  //hocemo da se vratimo u task u kom smo bili prije interapta
}



/*LEDOVKE*/
static void LED_bar_Task(void* pvParameters) {
	uint8_t led_tmp, tmp_cifra, led_tmp1, i;

	static char tmp_string[20];

	for (;;) {
		printf("LED FUNKCIJA\n");
		if (xSemaphoreTake(LED_INT_BinarySemaphore1, portMAX_DELAY) != pdTRUE) { 
			printf("Greska sem take \n");
		}
		//ovdje je dosao ako je neko kliknuo na led bar, ako se desio interrap za ledovke
		if (get_LED_BAR((uint8_t)0, &led_tmp) != 0) { //prvi argument je broj stupca-nulti stubac, sa njega prosljedjujemo 8-bitni broj
			printf("Greska_get\n");
		}
		printf("LED_TMP %d \n", led_tmp);
		led_tmp1 = led_tmp; //11000

		for (i = (uint8_t)0; i <= (uint8_t)4; i++) {   //interesuju nas prve 4 pozicije
			tmp_cifra = led_tmp1 % (uint8_t)2;
			led_tmp1 = led_tmp1 / (uint8_t)2;   //broj(int) koji smo dobili predstavljamo binarno
			tmp_string[i] = (char)tmp_cifra;   //+48 pretvaramo onda u string(char)
		}
		tmp_string[5] = '\0';
		printf("STRING LEDOVKE %s \n", tmp_string);
		if (xQueueSend(serial_queue, &tmp_string, 0) != pdTRUE) {   //stavljamo sve u red iz ledovki
			printf("Greska_get\n");
		}
	}
}



/*RECEIVE1*/
static void SerialReceive1_Task(void* pvParameters)
{
	uint8_t cc = 0;
	char tmp_str[100], string_queue[100];
	static uint8_t i = 0, tmp;

	for (;;) //automatski+, manuelno+, brzina120+, nivo 3 0+
	{

		if (xSemaphoreTake(RXC_BinarySemaphore1, portMAX_DELAY) != pdTRUE) {
			printf("Greska em take1 \n");
		}
		printf("Ako smo kliknuli na send text - kanal 1\n");  
		if (get_serial_character(COM_CH1, &cc) != 0) { 
			printf("Greska_get\n");
		}
		printf("karakter koji pristize %c\n", cc);
		if (cc != (uint8_t)43) {
			if (cc >= (uint8_t)65 && cc <= (uint8_t)90) { 
				tmp = cc + (uint8_t)32;      //velika slova prebacujemo u mala
				tmp_str[i] = (char)tmp;
				i++;
			}
			else {
				tmp_str[i] = (char)cc;     //mala zadrzavamo
				i++;
			}
		}
		else {
			tmp_str[i] = '\0';   //na mjestu plusa stavi terminator
			i = 0;
			printf("String sa serijske, man, auto %s \n", tmp_str);
			strcpy(string_queue, tmp_str);  //kopira string u red

			if (xQueueSend(serial_queue, &string_queue, 0) != pdTRUE) {     //smjestamo u red sve sa kanala1
				printf("Greska_get\n");
			}
			printf("Red za task 3 \n");
		}
	}
}



/*OBRADA SENZORA*/
static void Obrada_Senzora(void* pvParameters) {
	
	char string_queue[200], tmp_str1[100];
	static uint8_t i = 7, j = 8, k = 5, switch_pos = 6, cifra, index_brzine = 0, index_brzine1 = 0;
	static uint16_t suma = 0, suma1 = 0;
	static float suma_brzine = (float)0, suma_brzine1 = (float)0;

	char tmp;

	for (;;)
	{
		printf("pre primanja reda - obrada\n");

		if (xQueueReceive(serial_queue, &string_queue, portMAX_DELAY) != pdTRUE) {  //iz reda smjestamo u string
			printf("Greska\n");
		}
		string_queue[len] = '\0';
		//printf("Posle primanja reda reda\n");
		printf("STRING : %s \n", string_queue);


		//DETEKTUJE O KOM PRIJEMU SE RADU
		
		//STRCMP ako su jednaka dva stringa vraca 0
		if (strcmp(string_queue, "automatski\0") == 0) {//automatski
			switch_pos = (uint8_t)0;
			asinhron = 1;
			auto_man = 1;
			printf("ovde usao ako je stiglo automatski");
		}
		else if (strcmp(string_queue, "manuelno\0") == 0) {//manuelno
			switch_pos = (uint8_t)1;
			asinhron = 1;
			auto_man = 1;
		}
		else if (string_queue[0] == 'b' && string_queue[1] == 'r' && string_queue[2] == 'z' //brzina
			&& string_queue[3] == 'i' && string_queue[4] == 'n' && string_queue[5] == 'a'
			&& string_queue[6] == (char)32) { 
			switch_pos = (uint8_t)2;

		}
		else if ((string_queue[0] == '0' || string_queue[0] == '1') && string_queue[1] == (char)32   //vrijednost_senzora
			&& (string_queue[2] == '0' || string_queue[2] == '1') && string_queue[3] == (char)32
			&& (string_queue[4] == '0' || string_queue[4] == '1') && string_queue[5] == (char)32
			&& (string_queue[6] == '0' || string_queue[6] == '1') && string_queue[7] == (char)32) {  
			switch_pos = (uint8_t)3;

			//MISRA 15.7: IGNORISALI ELSE iz razloga sto u slucaju da nista nije ispunjeno, nista i ne treba da radi
		}
		else if ((string_queue[0] == '0' || string_queue[0] == '1')    //ledovke
			&& (string_queue[1] == '0' || string_queue[1] == '1')
			&& (string_queue[2] == '0' || string_queue[2] == '1')
			&& (string_queue[3] == '0' || string_queue[3] == '1')) {
			switch_pos = 4;


		}
		else if (string_queue[0] == 'n' && string_queue[1] == 'i' && string_queue[2] == 'v'
			&& string_queue[3] == 'o' && string_queue[4] == (char)32 && (string_queue[5] >= '1' && string_queue[5] <= '4')
			&& string_queue[6] == (char)32 && (string_queue[7] == '0' || string_queue[7] == '1')) {


			broj = (uint8_t)string_queue[5] - (uint8_t)48;
			NIVO = (uint8_t)string_queue[7] - (uint8_t)48;
			asinhron = 1;

		}
		else {
			printf("Usao u else, misra\n");
		}

		switch (switch_pos) {
		case 0:printf("AUTOMATSKI \n");   //AUTOMATSKI
			rezim_rada = 0;
			break;

		case 1:printf("MANUELNO \n");    //MANUELNO
			rezim_rada = (uint8_t)1;
			break;

		case 2: printf("BRZINA \n"); //brzina 120
			while (string_queue[i] != '\0') {   //izvlacimo broj 120
				if (string_queue[i] >= '0' && string_queue[i] <= '9') {
					cifra = (uint8_t)string_queue[i++] - (uint8_t)48;   //krece od 7. pozicije i=7, tada pocinje broj 120
																		//iz chara pretvaramo u cifru, oduzimamo 48
					suma = suma * (uint16_t)10 + (uint16_t)cifra;       
				}
			}
			i = 7;
			Vmax = suma;
			suma = (uint16_t)0;

			printf("Vmax %d\n", Vmax);
			break;

		case 3: printf("PROZORI\n"); // 0 1 0 1 320+
			if (rezim_rada == (uint8_t)1) {      //MANUELNO
				j = 8;
				while (string_queue[j] != '\0') {   //izvlacimo broj 320 i krecemo od 8. pozicije j=8
					if (string_queue[j] >= '0' && string_queue[j] <= '9') {
						cifra = (uint8_t)string_queue[j++] - (uint8_t)48;
						suma1 = suma1 * (uint16_t)10 + (uint16_t)cifra;
					}

				}
				Vtrenutna = suma1;// usrednjavanje ove vrednosti, 10 puta sabiramo
				if (index_brzine1 < (uint8_t)10) {    //sabiramo 10 prvih brzina i smjestimo u usuma_brzine1
					suma_brzine1 = suma_brzine1 + (float)Vtrenutna;
					index_brzine1++;
				}
				else {

					Vsrednja = (float)suma_brzine1 / (float)10;  //usrednjavamo

					suma_brzine1 = (float)0;  //vrati na 0 sve
					index_brzine1 = 0;
					printf("Vsrednja %f\n", Vsrednja);
				}
				if (Vtrenutna > Vmax_display) {
					Vmax_display = Vtrenutna;      //nova maximalna
					printf("VMAX_DISPLAY%d\n", Vmax_display);
				}
				suma1 = 0;
				j = 8;
			}
			else	if (rezim_rada == (uint8_t)0) {   //AUTOMATSKI

				j = 8;
				while (string_queue[j] != '\0') { //320
					if (string_queue[j] >= '0' && string_queue[j] <= '9') {
						cifra = (uint8_t)string_queue[j++] - (uint8_t)48;
						suma1 = suma1 * (uint16_t)10 + (uint16_t)cifra;
					}

				}
				Vtrenutna = suma1;// usrednjavanje ove vrijednosti, 10 puta sabiramo
				if (Vtrenutna > Vmax_display) {
					Vmax_display = Vtrenutna;
					printf("VMAX_DISPLAY %d\n", Vmax_display);
				}
				if (index_brzine < (uint8_t)10) {
					suma_brzine = suma_brzine + (float)Vtrenutna;
					index_brzine++;
				}
				else {

					Vsrednja = (float)suma_brzine / (float)10;

					suma_brzine = (float)0;
					index_brzine = (uint8_t)0;
					printf("Vsrednja %f\n", Vsrednja);
				}


				if (Vsrednja < (my_float)Vmax) {   //ostaju te iste vrijednosti sa senzora
					L_P = (uint8_t)string_queue[0] - (uint8_t)48;   //izdvojimo sa tih pozicija, oduzmemo 48 da pretvorimo u cifre
					D_P = (uint8_t)string_queue[2] - (uint8_t)48;
					L_Z = (uint8_t)string_queue[4] - (uint8_t)48;
					D_Z = (uint8_t)string_queue[6] - (uint8_t)48;
				}
				else {   //ako je Vsrednja > Vmax zatvaramo prozore
					L_P = 1;
					D_P = 1;
					L_Z = 1;
					D_Z = 1;
				}

				suma1 = 0;
				j = 8;

				printf("Vtrenutna %d\n", Vtrenutna);
				printf("L_P %d\n", L_P);
				printf("D_P %d\n", D_P);
				printf("L_Z %d\n", L_Z);
				printf("D_Z %d\n", D_Z);
			}

			break;
		case 4: printf("LEDOVKE\n");

			taster_display = (uint8_t)string_queue[4] - (uint8_t)48;  //nezavisno od toga da li rezim 0 ili 1


			if (rezim_rada == (uint8_t)1) {   
				L_P = (uint8_t)string_queue[0] - (uint8_t)48;   //procitamo vrijednosti sa ledovki
				D_P = (uint8_t)string_queue[1] - (uint8_t)48;
				L_Z = (uint8_t)string_queue[2] - (uint8_t)48;
				D_Z = (uint8_t)string_queue[3] - (uint8_t)48;

				printf("L_P %d\n", L_P);
				printf("D_P %d\n", D_P);
				printf("L_Z %d\n", L_Z);
				printf("D_Z %d\n", D_Z);
			}



			break;

		default: printf("Usao u default\n");
			break;
		}


	}

}

static void SerialSend0_Task(void* pvParameters)
{
	//configASSERT(pvParameters);
	static uint8_t t_point = 0;
	static uint32_t brojac = 0;
	static uint8_t trep = 1;
	static uint8_t led_vrednost = 0x00;
	static const uint8_t c1 = (uint8_t)'T';
	static char tmp_str[50], tmp_str1[10];
	static uint8_t i = 0, k = 0, j = 0;
	static uint8_t tmp_cifra = 0;
	static uint16_t tmp_broj = 0;
	static char string_manuelno[10], tmp_str2[10];
	static char string_automatski[12];
	static int brojac_provera = 0;

	string_manuelno[0] = 'm';
	string_manuelno[1] = 'a';
	string_manuelno[2] = 'n';
	string_manuelno[3] = 'u';
	string_manuelno[4] = 'e';
	string_manuelno[5] = 'l';
	string_manuelno[6] = 'n';
	string_manuelno[7] = 'o';
	string_manuelno[8] = ' ';
	string_manuelno[9] = '\0';

	string_automatski[0] = 'a';
	string_automatski[1] = 'u';
	string_automatski[2] = 't';
	string_automatski[3] = 'o';
	string_automatski[4] = 'm';
	string_automatski[5] = 'a';
	string_automatski[6] = 't';
	string_automatski[7] = 's';
	string_automatski[8] = 'k';
	string_automatski[9] = 'i';
	string_automatski[10] = ' ';
	string_automatski[11] = '\0';
	for (;;)
	{
	
		if (xSemaphoreTake(Send_BinarySemaphore, portMAX_DELAY) != pdTRUE) {    //brojac     0-25   200ms*25=5s
			printf("GReska take\n");
		};
		printf("Proslo je 5s \n");

		if ((Vsrednja > (my_float)Vmax) && (rezim_rada == (uint8_t)0)) {
			L_P = 1;
			D_P = 1;
			L_Z = 1;
			D_Z = 1;
		}

		if (asinhron == (uint8_t)0) {            //nije automatski, manuelno, nivo
			                                     //salje samo stanje senzora i brzinu
			tmp_str[0] = L_P + (char)48;
			tmp_str[1] = (char)32;
			tmp_str[2] = D_P + (char)48;
			tmp_str[3] = (char)32;
			tmp_str[4] = L_Z + (char)48;
			tmp_str[5] = (char)32;
			tmp_str[6] = D_Z + (char)48;
			tmp_str[7] = (char)45;
			
			for (i = (uint8_t)0; i <= (uint8_t)7; i++) {   // od 0. do 7. karaktera su prozori
				if (send_serial_character((uint8_t)COM_CH1,(uint8_t)tmp_str[i]) != 0) { //SLANJE PROZORA
					printf("Greska_send \n");
				}
				vTaskDelay(pdMS_TO_TICKS(100));
			}
			k = (uint8_t)0;
			tmp_broj = (uint16_t)Vsrednja; //123  char
			printf("VSREDNJA %d\n", tmp_broj);
			while (tmp_broj != (uint16_t)0) {
				tmp_cifra = (uint8_t)tmp_broj % (uint8_t)10; //3, 2
				tmp_broj = tmp_broj / (uint16_t)10; //12
				tmp_str1[k] = tmp_cifra + (char)48; // 3 2 1  int
				k++;
			}
			j = 1;
			printf("K: %d\n", k);
			if (k != (uint8_t)0) {    //obrne ga kad ga salje
				while (k != (uint8_t)0) {
					brojac_provera++;
					if (send_serial_character((uint8_t)COM_CH1, (uint8_t)tmp_str1[k - j]) != 0) { //SLANJE PROZORA
						printf("Greska_send \n");
					}
					k--;
					printf("Brojac_provera %d\n", brojac_provera);
					vTaskDelay(pdMS_TO_TICKS(100));
				}


			}
			if (send_serial_character(COM_CH1, 32) != 0) { //razmak
				printf("Greska_send \n");
			}
			vTaskDelay(pdMS_TO_TICKS(100));




			if (rezim_rada == (uint8_t)0) {     //automatski
				for (i = 0; i <= (uint8_t)10; i++) {
					if (send_serial_character((uint8_t)COM_CH1, (uint8_t)string_automatski[i]) != 0) { 
						printf("Greska_send \n");
					}
					vTaskDelay(pdMS_TO_TICKS(100)); //ubacujemo delay izmedju svaka dva karaktera
				}
			}
			else {                 //manuelno
				for (i = 0; i <= (uint8_t)8; i++) {
					if (send_serial_character((uint8_t)COM_CH1, (uint8_t)string_manuelno[i]) != 0) { 
						printf("Greska_send \n");
					}
					vTaskDelay(pdMS_TO_TICKS(100));
				}
			}



			if (send_serial_character(COM_CH1, 13) != 0) { //novi red
				printf("Greska_send \n");
			}


		}
		else if (asinhron == (uint8_t)1) {  //automatski, manuelno, nivo

			if (auto_man == (uint8_t)1) {  //ako je automatski il manuelno
				if (send_serial_character((uint8_t)COM_CH1, (uint8_t)'O') != 0) { //O
					printf("Greska_send \n");
				}
				vTaskDelay(pdMS_TO_TICKS(100));
				if (send_serial_character((uint8_t)COM_CH1, (uint8_t)'K') != 0) { //K
					printf("Greska_send \n");
				}
				vTaskDelay(pdMS_TO_TICKS(100));
				if (send_serial_character(COM_CH1, 13) != 0) { //novi red
					printf("Greska_send \n");
				}
				vTaskDelay(pdMS_TO_TICKS(100));
			}

			else {   //nivo prozora
				tmp_str2[0] = L_P + (char)48;
				tmp_str2[1] = (char)32;
				tmp_str2[2] = D_P + (char)48;
				tmp_str2[3] = (char)32;
				tmp_str2[4] = L_Z + (char)48;
				tmp_str2[5] = (char)32;
				tmp_str2[6] = D_Z + (char)48;
				tmp_str2[7] = (char)32;       //razmak
				tmp_str2[8] = (char)13;       //novi red
				

				//nivo 3 0+
				if (broj == (uint8_t)1) {             //broj predstavlja koji prozor mijenjamo
					if (NIVO == (uint8_t)1) {         //nivo predstavlja stanje tog prozora
						tmp_str2[0] = (char)49;       //nivo = 1
					}
					else if (NIVO == (uint8_t)0) {
						tmp_str2[0] = (char)48;       //nivo = 0
					}
				}

				else if (broj == (uint8_t)2) {
					if (NIVO == (uint8_t)1) {
						tmp_str2[2] = (char)49;
					}
					else if (NIVO == (uint8_t)0) {
						tmp_str2[2] = (char)48;
					}
				}

				else if (broj == (uint8_t)3) {
					if (NIVO == (uint8_t)1) {
						tmp_str2[4] = (char)49;
					}
					else if (NIVO == (uint8_t)0) {
						tmp_str2[4] = (char)48;
					}
				}

				else if (broj == (uint8_t)4) {
					if (NIVO == (uint8_t)1) {
						tmp_str2[6] = (char)49;
					}
					else if (NIVO == (uint8_t)0) {
						tmp_str2[6] = (char)48;
					}
				}

				for (i = 0; i <= (uint8_t)8; i++) {  //for prolazi kroz cijeli string i ispisuje kako izgledaju senzori
					if (send_serial_character((uint8_t)COM_CH1, (uint8_t)tmp_str2[i]) != 0) {
						printf("Greska_send \n");
					}
					vTaskDelay(pdMS_TO_TICKS(100));
				}

			}
			asinhron = 0;  //vratimo na 0
			auto_man = 0;

		}


	}

}


static void Display_Task(void* pvParameters) {
	
	static uint8_t i = 0, j = 0, tmp_cifra, tmp_cifra1;
	static uint16_t tmp_broj = 0, tmp_broj1 = 0;

	for (;;)
	{
		if (xSemaphoreTake(Display_BinarySemaphore, portMAX_DELAY) != pdTRUE) {  //brojac  2-7  200ms*5=1s za osvjezavanje displeja
			printf("Greska take\n");
		}
		if (select_7seg_digit(0) != 0) {  //selektujemo nultu cifru
			printf("Greska_select \n");   
		}
		if (set_7seg_digit(hex[rezim_rada]) != 0) {    //postavimo rezim rada
			printf("Greska_set \n");
		}

		tmp_broj = (uint8_t)Vtrenutna; //ispisujemo trenutnu brzinu
		i = 0;
		while (tmp_broj != (uint8_t)0) {
			tmp_cifra = (uint8_t)tmp_broj % (uint8_t)10;

			if (select_7seg_digit((uint8_t)4 - i) != 0) {   //selektujemo od 4. poziciju
				printf("Greska_select \n");
			}
			if (set_7seg_digit(hex[tmp_cifra]) != 0) {      //upisemo od 123 3
				printf("Greska_set \n");
			}
			tmp_broj = tmp_broj / (uint8_t)10;  //dobili 12...i tako se nastavlja u krug while
			i++;
		}

		if (i == (uint8_t)2) {                  //treba da se precrta cifru stotina ako je brzina dvocifrena 80
			if (select_7seg_digit(2) != 0) {
				printf("Greska_select \n");
			}
			if (set_7seg_digit(hex[0]) != 0) {
				printf("Greska_set \n");
			}
		}
		else if (i == (uint8_t)1) {              //precrtamo cifru stotina i desetica ako je brzina jednocifrena
			if (select_7seg_digit(2) != 0) {
				printf("Greska_select \n");
			}
			if (set_7seg_digit(0x00) != 0) {
				printf("Greska_set \n");
			}
			if (select_7seg_digit(3) != 0) {     
				printf("Greska_select \n");
			}
			if (set_7seg_digit(0x00) != 0) {     
				printf("Greska_set \n");
			}

		}

		if (taster_display == (uint8_t)1) {        //kada je pritisnut taster za displej na led baru
			printf("*********************\n");
			taster_display = 0;
			tmp_broj1 = Vmax_display;
			j = 0;

			while (tmp_broj1 != (uint8_t)0) {
				tmp_cifra1 = (uint8_t)tmp_broj1 % (uint8_t)10;
				if (select_7seg_digit((uint8_t)8 - j) != 0) {     //selektujemo od 8. cifre
					printf("Greska_select \n");
				}
				if (set_7seg_digit(hex[tmp_cifra1]) != 0) {       //upisujemo od 456 6
					printf("Greska_set \n");
				}
				tmp_broj1 = tmp_broj1 / (uint16_t)10;      //dobili 45...i tako se nastavlja u krug
				j++;

			}
		}


	}
}




static void TimerCallBack(TimerHandle_t timer)//upitno dal je STATIC
{
	static uint32_t brojac1 = 0, brojac2 = 2;

	if (send_serial_character((uint8_t)COM_CH0, (uint8_t)'T') != 0) { //SLANJE TRIGGER SIGNALA svakih 200ms
		printf("Greska_send \n");
	}
	brojac1++;
	brojac2++;

	if (brojac1 == (uint32_t)25) {      //5s 
		brojac1 = (uint32_t)0;
		if (xSemaphoreGive(Send_BinarySemaphore, 0) != pdTRUE) {
			printf("GRESKA");
		}
	}
	if (brojac2 == (uint32_t)7) {       //1s osvjezavanje displeja
		brojac2 = (uint32_t)2;
		if (xSemaphoreGive(Display_BinarySemaphore, 0) != pdTRUE) {
			printf("DISPLAY GRESKA SEMAFOR\n");
		}
	}


}




/* MAIN - SYSTEM STARTUP POINT */
void main_demo(void)
{
	if (init_LED_comm() != 0) {
		printf("Neuspesna inicijalizacija \n");
	}
	if (init_7seg_comm() != 0) {
		printf("Neuspesna inicijalizacija \n");
	}
	//samo primamo podatke sa serijske
	if (init_serial_downlink(COM_CH0) != 0) {
		printf("Neuspesna inicijalizacija \n");
	}
	if (init_serial_uplink(COM_CH0) != 0) {
		printf("Neuspesna inicijalizacija \n");
	}
	if (init_serial_downlink(COM_CH1) != 0) {
		printf("Neuspesna inicijalizacija \n");
	}
	if (init_serial_uplink(COM_CH1) != 0) {
		printf("Neuspesna inicijalizacija \n");
	}

	/* Create LED interrapt semaphore */
	LED_INT_BinarySemaphore1 = xSemaphoreCreateBinary();
	if (LED_INT_BinarySemaphore1 == NULL) {
		printf("Greska1\n");
	}
	Display_BinarySemaphore = xSemaphoreCreateBinary();
	Send_BinarySemaphore = xSemaphoreCreateBinary();
	if (Display_BinarySemaphore == NULL) {
		printf("Greska1 \n");
	}
	BaseType_t status;
	//task za obradu senzora
	status = xTaskCreate(Obrada_Senzora, "obrada senzora", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)obrada_rezultata, NULL);
	if (status != pdPASS) {
		for (;;) {}
	}

	status = xTaskCreate(LED_bar_Task, "start", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)ledovke, NULL);
	if (status != pdPASS)
	{
		for (;;) {}
	}

	serial_queue = xQueueCreate(1, 12u * sizeof(char));
	if (serial_queue == NULL) {
		printf("Greska1\n");
	}

	timer1 = xTimerCreate(
		"timer",
		pdMS_TO_TICKS(200),
		pdTRUE,
		NULL,
		TimerCallBack);
	if (timer1 == NULL) {
		printf("Greska prilikom kreiranja\n");
	}
	if (xTimerStart(timer1, 0) != pdPASS) {
		printf("Greska prilikom kreiranja\n");
	}
	/* SERIAL TRANSMITTER TASK */
	//xTimerStart(timer1, 0);
	/* SERIAL RECEIVER TASK */
	//TASK 2
	status = xTaskCreate(SerialReceive0_Task, "SRx", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)prijem_kanal0, NULL);
	if (status != pdPASS) {
		for (;;) {}
	}
	r_point = (uint8_t)0;

	status = xTaskCreate(SerialReceive1_Task, "SRx", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)prijem_kanal1, NULL);
	if (status != pdPASS) {
		for (;;) {}
	}
	r_point = (uint8_t)0;

	//TASK 2b
	status = xTaskCreate(SerialSend0_Task, "STx", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)slanje_kanal0, NULL);
	if (status != pdPASS) {
		for (;;) {}
	}
	status = xTaskCreate(Display_Task, "display", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)display, NULL);

	if (status != pdPASS) {
		for (;;) {}
	}

	/* Create TBE semaphore - serial transmit comm */
	RXC_BinarySemaphore0 = xSemaphoreCreateBinary();
	if (RXC_BinarySemaphore0 == NULL) {
		printf("Greskasem\n");
	}
	RXC_BinarySemaphore1 = xSemaphoreCreateBinary();
	if (RXC_BinarySemaphore1 == NULL) {
		printf("Greska1\n");
	}
	TXC_BinarySemaphore = xSemaphoreCreateBinary();
	if (TXC_BinarySemaphore == NULL) {
		printf("Greska1\n");
	}
	/* SERIAL RECEPTION INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_RXC, prvProcessRXCInterrupt);
	vPortSetInterruptHandler(portINTERRUPT_SRL_OIC, OnLED_ChangeInterrupt);

	vTaskStartScheduler();  //vrsi raspored po prioritetima
	printf("Resen issue\n");
	for (;;) {}
}







