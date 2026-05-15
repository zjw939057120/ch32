/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/06/06
 * Description        : Main program body.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/*
 *@Note
 two-wire full duplex mode, master/slave mode, data transceiver:
 Master:SPI1_SCK(PA5)\SPI1_MISO(PA6)\SPI1_MOSI(PA7).
 Slave:SPI1_SCK(PA5)\SPI1_MISO(PA6)\SPI1_MOSI(PA7).

 This example demonstrates simultaneous full-duplex transmission and
 reception between Master and Slave.
 Note: The two boards download the Master and Slave programs respectively,
 and power on at the same time.
     Hardware connection:PA5 -- PA5
               PA6 -- PA6
               PA7 -- PA7

*/

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "string.h"

void SPI1_Init (void);
void SPI1_Send (void);