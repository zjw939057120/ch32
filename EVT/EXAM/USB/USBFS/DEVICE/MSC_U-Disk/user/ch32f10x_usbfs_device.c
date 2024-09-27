/********************************** (C) COPYRIGHT *******************************
 * File Name          : ch32f10x_usbfs_device.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2022/08/20
 * Description        : ch32f10x series usb interrupt Processing.
 *********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/

#include "ch32f10x_usbfs_device.h"
#include "SW_UDISK.h"
/*******************************************************************************/
/* Variable Definition */
/* Global */
const    uint8_t  *pUSBFS_Descr;

/* Setup Request */
volatile uint8_t  USBFS_SetupReqCode;
volatile uint8_t  USBFS_SetupReqType;
volatile uint16_t USBFS_SetupReqValue;
volatile uint16_t USBFS_SetupReqIndex;
volatile uint16_t USBFS_SetupReqLen;

/* USB Device Status */
volatile uint8_t  USBFS_DevConfig;
volatile uint8_t  USBFS_DevAddr;
volatile uint8_t  USBFS_DevSleepStatus;
volatile uint8_t  USBFS_DevEnumStatus;

/* Endpoint Buffer */
__attribute__ ((aligned(4))) uint8_t USBFS_EP0_Buf[ DEF_USBD_UEP0_SIZE ];    //ep0(64)
__attribute__ ((aligned(4))) uint8_t UDisk_In_Buf[ DEF_UDISK_PACK_64 ];
__attribute__ ((aligned(4))) uint8_t UDisk_Out_Buf[ DEF_UDISK_PACK_64 ];

/* USB IN Endpoint Busy Flag */
volatile uint8_t  USBFS_Endp_Busy[ DEF_UEP_NUM ];


/*********************************************************************
 * @fn      USBFS_RCC_Init
 *
 * @brief   Set USB clock.
 *
 * @return  none
 */
void USBFS_RCC_Init( void )
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    EXTEN->EXTEN_CTR |= EXTEN_USBFS_IO_EN;
    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_1Div5);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBFS,ENABLE);
}

/*********************************************************************
 * @fn      USBFS_Device_Endp_Init
 *
 * @brief   Initializes USB device endpoints.
 *
 * @return  none
 */
void USBFS_Device_Endp_Init( void )
{
		uint8_t i;
	
    R8_UEP2_3_MOD = RB_UEP2_TX_EN | RB_UEP3_RX_EN;

    pEP0_RAM_Addr = USBFS_EP0_Buf;
    R16_UEP0_DMA = (uint16_t)(uint32_t)USBFS_EP0_Buf;

    R16_UEP2_DMA = (uint16_t)(uint32_t)UDisk_In_Buf;
    R16_UEP3_DMA = (uint16_t)(uint32_t)UDisk_Out_Buf;


    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP3_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP2_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;

    /* Clear End-points Busy Status */
    /* Clear End-points Busy Status */
    for( i = 0; i < DEF_UEP_NUM; i++ )
    {
        USBFS_Endp_Busy[ i ] = 0;
    }
}

/*********************************************************************
 * @fn      USBFS_Device_Init
 *
 * @brief   Initializes USB device.
 *
 * @return  none
 */
void USBFS_Device_Init( FunctionalState sta , PWR_VDD VDD_Voltage)
{
		if( VDD_Voltage == PWR_VDD_5V)
		{
			EXTEN->EXTEN_CTR |= EXTEN_USB_5V_SEL;
		}
		else
		{
			EXTEN->EXTEN_CTR &= ~EXTEN_USB_5V_SEL;
		}
		
    if( sta )
    {
		R8_USB_CTRL = RB_UC_RESET_SIE | RB_UC_CLR_ALL;
        Delay_Us( 10 );
        R8_USB_CTRL = 0x00;
        R8_USB_INT_EN = RB_UIE_SUSPEND | RB_UIE_BUS_RST | RB_UIE_TRANSFER;
        R8_USB_CTRL = RB_UC_DEV_PU_EN | RB_UC_INT_BUSY | RB_UC_DMA_EN;
		USBFS_Device_Endp_Init( );
        R8_UDEV_CTRL = RB_UD_PD_DIS | RB_UD_PORT_EN;
        NVIC_EnableIRQ(USBFS_IRQn);
    }
    else
    {
        R8_USB_CTRL = RB_UC_RESET_SIE | RB_UC_CLR_ALL;
        Delay_Us( 10 );
        R8_USB_CTRL = 0x00;
        NVIC_DisableIRQ(USBFS_IRQn);
    }
}

/*********************************************************************
 * @fn      USBFS_Endp_DataUp
 *
 * @brief   usbfs device data upload
 *          input: endp  - end-point numbers
 *                 *pubf - data buffer
 *                 len   - load data length
 *                 mod   - 0: DEF_UEP_DMA_LOAD 1: DEF_UEP_CPY_LOAD
 *
 * @return  none
 */
uint8_t USBFS_Endp_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len, uint8_t mod)
{
    uint8_t data_endp = endp;
    uint16_t data_len = len;
    
    if(data_endp == DEF_UEP1)
    {
        /* Check if endp1 upload end  */
        if(USBFS_Endp_Busy[DEF_UEP1])
        {
            return 1;
        }
        if( (R8_UEP4_1_MOD & RB_UEP1_TX_EN) && (R8_UEP4_1_MOD & RB_UEP1_RX_EN) )
        {
            /* enpd1 tx/rx enable, 64-bytes out, 64-bytes in */
            memcpy( ((uint8_t *)(R16_UEP1_DMA + 0x20000000)) + 64, pbuf, data_len );
        }
        else if( R8_UEP4_1_MOD & RB_UEP1_TX_EN )
        {
            /* enpd1 tx enable, 64-bytes in */
            if( mod == DEF_UEP_DMA_LOAD )
            {
                R16_UEP1_DMA = (uint16_t)(uint32_t)pbuf;
            }
            else 
            {
                memcpy( ((uint8_t *)(R16_UEP1_DMA + 0x20000000)), pbuf, data_len );
            }
        }
        else 
        {
            return 1;
        }
        USBFS_Endp_Busy[DEF_UEP1] = 0x01;
        R8_UEP1_T_LEN = data_len;
        R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
    }
    else if(data_endp == DEF_UEP2)
    {
        /* Check if endp2 upload end  */
        if(USBFS_Endp_Busy[DEF_UEP2])
        {
            return 1;
        }
        if( (R8_UEP2_3_MOD & RB_UEP2_TX_EN) && (R8_UEP2_3_MOD & RB_UEP2_RX_EN) )
        {
            /* enpd2 tx/rx enable, 64-bytes out, 64-bytes in */
            memcpy( ((uint8_t *)(R16_UEP2_DMA + 0x20000000)) + 64, pbuf, data_len );
        }
        else if( R8_UEP2_3_MOD & RB_UEP2_TX_EN )
        {
            /* enpd2 tx enable, 64-bytes in */
            if( mod == DEF_UEP_DMA_LOAD )
            {
                R16_UEP2_DMA = (uint16_t)(uint32_t)pbuf;
            }
            else 
            {
                memcpy( ((uint8_t *)(R16_UEP2_DMA + 0x20000000)), pbuf, data_len );
            }
        }
        else 
        {
            return 1;
        }
        USBFS_Endp_Busy[DEF_UEP2] = 0x01;
        R8_UEP2_T_LEN = data_len;
        R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
    }
    else if(data_endp == DEF_UEP3)
    {
        /* Check if endp3 upload end  */
        if(USBFS_Endp_Busy[DEF_UEP3])
        {
            return 1;
        }
        if( (R8_UEP2_3_MOD & RB_UEP3_TX_EN) && (R8_UEP2_3_MOD & RB_UEP3_RX_EN) )
        {
            /* enpd3 tx/rx enable, 64-bytes out, 64-bytes in */
            memcpy( ((uint8_t *)(R16_UEP3_DMA + 0x20000000)) + 64, pbuf, data_len );
        }
        else if( R8_UEP2_3_MOD & RB_UEP3_TX_EN )
        {
            /* enpd3 tx enable, 64-bytes in */
            if( mod == DEF_UEP_DMA_LOAD )
            {
                R16_UEP3_DMA = (uint16_t)(uint32_t)pbuf;
            }
            else 
            {
                memcpy( ((uint8_t *)(R16_UEP3_DMA + 0x20000000)), pbuf, data_len );
            }
        }
        else 
        {
            return 1;
        }
        USBFS_Endp_Busy[DEF_UEP3] = 0x01;
        R8_UEP3_T_LEN = data_len;
        R8_UEP3_CTRL = (R8_UEP3_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
    }
    else if(data_endp == DEF_UEP4)
    {
        if(USBFS_Endp_Busy[DEF_UEP4])
        {
            return 1;
        }
        if( (R8_UEP4_1_MOD & RB_UEP4_TX_EN) && (R8_UEP4_1_MOD & RB_UEP4_RX_EN) )
        {
            /* enpd4 tx/rx enable, share dma with endp0 */
            memcpy( ((uint8_t *)(R16_UEP0_DMA + 0x20000000)) + 128, pbuf, data_len );
        }
        else if( R8_UEP4_1_MOD & RB_UEP4_TX_EN )
        {
            /* enpd4 only tx enable, share dma with endp0 */
            memcpy( ((uint8_t *)(R16_UEP0_DMA + 0x20000000)) + 64, pbuf, data_len );
        }
        else 
        {
             return 1;
        }
        USBFS_Endp_Busy[DEF_UEP4] = 0x01;
        R8_UEP4_T_LEN = data_len;
        R8_UEP4_CTRL = (R8_UEP4_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
    }
    else
    {
        return 1;
    }
    return 0;
}

/*********************************************************************
 * @fn      USBFS_IRQHandler
 *
 * @brief   This function handles USB FS exception.
 *
 * @return  none
 */
void USBFS_IRQHandler( void )
{
    uint8_t  intflag, intst, errflag;
    uint16_t len;

    intflag = R8_USB_INT_FG;
    intst   = R8_USB_INT_ST;

    if( intflag & RB_UIF_TRANSFER )
    {
        switch ( intst & MASK_UIS_TOKEN )
        {
            /* data-in stage processing */
            case UIS_TOKEN_IN:
                switch ( intst & ( MASK_UIS_TOKEN | MASK_UIS_ENDP ) )
                {
                    /* end-point 0 data in interrupt */
                    case UIS_TOKEN_IN | DEF_UEP0:
                        if( USBFS_SetupReqLen == 0 )
                        {
                            R8_UEP0_CTRL = (R8_UEP0_CTRL & ~MASK_UEP_R_RES ) | UEP_R_RES_ACK;
                        }
                        if ( ( USBFS_SetupReqType & USB_REQ_TYP_MASK ) != USB_REQ_TYP_STANDARD )
                        {
                            /* Non-standard request endpoint 0 Data upload */
                        }
                        else
                        {
                            switch( USBFS_SetupReqCode )
                            {
                                case USB_GET_DESCRIPTOR:
                                        len = USBFS_SetupReqLen >= DEF_USBD_UEP0_SIZE ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                                        memcpy( USBFS_EP0_Buf, pUSBFS_Descr, len );
                                        USBFS_SetupReqLen -= len;
                                        pUSBFS_Descr += len;
                                        R8_UEP0_T_LEN   = len;
                                        R8_UEP0_CTRL ^= RB_UEP_T_TOG;
                                        break;

                                case USB_SET_ADDRESS:
                                        R8_USB_DEV_AD = ( R8_USB_DEV_AD & RB_UDA_GP_BIT ) | USBFS_DevAddr;
                                        break;

                                default:
                                        break;
                            }
                        }
                        break;

                        /* end-point 2 data in interrupt */
                        case UIS_TOKEN_IN | DEF_UEP2:
                            R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                            R8_UEP2_CTRL ^= RB_UEP_T_TOG;
                            USBFS_Endp_Busy[ DEF_UEP2 ] = 0;
							UDISK_In_EP_Deal();
                            break;
                    default :
                        break;
                }
                break;

            /* data-out stage processing */
            case UIS_TOKEN_OUT:
                switch ( intst & ( MASK_UIS_TOKEN | MASK_UIS_ENDP ) )
                {
                    /* end-point 0 data out interrupt */
                    case UIS_TOKEN_OUT | DEF_UEP0:
                        if ( intst & RB_UIS_TOG_OK )
                        {
                            len = R8_USB_RX_LEN;
                            if ( intst & RB_UIS_TOG_OK )
                            {
                                if ( ( USBFS_SetupReqType & USB_REQ_TYP_MASK ) != USB_REQ_TYP_STANDARD )
                                {
                                    /* Non-standard request end-point 0 Data download */
                                    /* Add your code here */
                                }
                                else
                                {
                                    /* Standard request end-point 0 Data download */
                                    /* Add your code here */
                                }
                                if( USBFS_SetupReqLen == 0 )
                                {
                                    R8_UEP0_T_LEN = 0;
                                    R8_UEP0_CTRL = (R8_UEP0_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK | RB_UEP_T_TOG;
                                }
                            }
                            else
                            {
                                /* Standard request end-point 0 Data download */
                            }
                        }
                        break;

                    /* end-point 3 data out interrupt */
                    case UIS_TOKEN_OUT | DEF_UEP3:
                        if ( intst & RB_UIS_TOG_OK )
                        {
                            len = R8_USB_RX_LEN;
							R8_UEP3_CTRL ^= RB_UEP_R_TOG;
                            R8_UEP3_CTRL = (R8_UEP3_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_NAK;
                            UDISK_Out_EP_Deal(UDisk_Out_Buf,len);
							R8_UEP3_CTRL = (R8_UEP3_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_ACK;
                        }
                        break;
                        
                    default:
                        break;
                }
                break;

            /* Setup stage processing */
            case UIS_TOKEN_SETUP:
                R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_NAK | UEP_T_RES_NAK;
                /* Store All Setup Values */
                USBFS_SetupReqType  = pUSBFS_SetupReqPak->bRequestType;
                USBFS_SetupReqCode  = pUSBFS_SetupReqPak->bRequest;
                USBFS_SetupReqLen   = pUSBFS_SetupReqPak->wLength;
                USBFS_SetupReqValue = pUSBFS_SetupReqPak->wValue;
                USBFS_SetupReqIndex = pUSBFS_SetupReqPak->wIndex;
                len = 0;
                errflag = 0;
                if ( ( USBFS_SetupReqType & USB_REQ_TYP_MASK ) != USB_REQ_TYP_STANDARD )
                {
                    /* usb non-standard request processing */
                    if (( USBFS_SetupReqType & USB_REQ_TYP_MASK ) == USB_REQ_TYP_CLASS)
                    {
                        if (USBFS_SetupReqCode == CMD_UDISK_GET_MAX_LUN)
                        {
                            USBFS_EP0_Buf[0] = 0;
                            pUSBFS_Descr = (uint8_t*)USBFS_EP0_Buf;
                            len = 1;
                        }
                        else if (USBFS_SetupReqCode == CMD_UDISK_RESET)
                        {
                            /* UDisk Reset */
                            Udisk_Sense_Key = 0x00;
                            Udisk_Sense_ASC = 0x00;
                            Udisk_CSW_Status = 0x00;
                            Udisk_Transfer_Status = 0x00;
                            UDISK_Transfer_DataLen = 0x00;
                        }
                        else
                        {
                            errflag = 0xFF;
                        }
                    }
                    else
                    {
                        errflag = 0xFF;
                    }

                }
                else
                {
                    /* usb standard request processing */
                    switch( USBFS_SetupReqCode )
                    {
                        /* get device/configuration/string/report/... descriptors */
                        case USB_GET_DESCRIPTOR:
                            switch( (uint8_t)( USBFS_SetupReqValue >> 8 ) )
                            {
                                /* get usb device descriptor */
                                case USB_DESCR_TYP_DEVICE:
                                    pUSBFS_Descr = MyDevDescr;
                                    len = DEF_USBD_DEVICE_DESC_LEN;
                                    break;

                                /* get usb configuration descriptor */
                                case USB_DESCR_TYP_CONFIG:
                                    pUSBFS_Descr = MyCfgDescr;
                                    len = DEF_USBD_CONFIG_DESC_LEN;
									break;

                                /* get usb string descriptor */
                                case USB_DESCR_TYP_STRING:
                                    switch( (uint8_t)( USBFS_SetupReqValue & 0xFF ) )
                                    {
                                        /* Descriptor 0, Language descriptor */
                                        case DEF_STRING_DESC_LANG:
                                            pUSBFS_Descr = MyLangDescr;
                                            len = DEF_USBD_LANG_DESC_LEN;
                                            break;

                                        /* Descriptor 1, Manufacturers String descriptor */
                                        case DEF_STRING_DESC_MANU:
                                            pUSBFS_Descr = MyManuInfo;
                                            len = DEF_USBD_MANU_DESC_LEN;
                                            break;

                                        /* Descriptor 2, Product String descriptor */
                                        case DEF_STRING_DESC_PROD:
                                            pUSBFS_Descr = MyProdInfo;
                                            len = DEF_USBD_PROD_DESC_LEN;
                                            break;

                                        /* Descriptor 3, Serial-number String descriptor */
                                        case DEF_STRING_DESC_SERN:
                                            pUSBFS_Descr = MySerNumInfo;
                                            len = DEF_USBD_SN_DESC_LEN;
                                            break;

                                            default:
                                                errflag = 0xFF;
                                                break;
                                        }
                                        break;
                                    default :
                                        errflag = 0xFF;
                                        break;
                                }

                            /* Copy Descriptors to Endp0 DMA buffer */
                            if( USBFS_SetupReqLen>len )
                            {
                                USBFS_SetupReqLen = len;
                            }
                            len = (USBFS_SetupReqLen >= DEF_USBD_UEP0_SIZE) ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                            memcpy( USBFS_EP0_Buf, pUSBFS_Descr, len );
                            pUSBFS_Descr += len;
                            break;

                        /* Set usb address */
                        case USB_SET_ADDRESS:
                            USBFS_DevAddr = (uint8_t)( USBFS_SetupReqValue & 0xFF );
                            break;

                        /* Get usb configuration now set */
                        case USB_GET_CONFIGURATION:
                            USBFS_EP0_Buf[0] = USBFS_DevConfig;
                            if ( USBFS_SetupReqLen > 1 )
                            {
                                USBFS_SetupReqLen = 1;
                            }
                            break;

                        /* Set usb configuration to use */
                        case USB_SET_CONFIGURATION:
                            USBFS_DevConfig = (uint8_t)( USBFS_SetupReqValue & 0xFF );
                            USBFS_DevEnumStatus = 0x01;
                            break;

                        /* Clear or disable one usb feature */
                        case USB_CLEAR_FEATURE:
                            if ( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_DEVICE )
                            {
                                /* clear one device feature */
                                if( (uint8_t)( USBFS_SetupReqValue & 0xFF ) == USB_REQ_FEAT_REMOTE_WAKEUP )
                                {
                                    /* clear usb sleep status, device not prepare to sleep */
                                    USBFS_DevSleepStatus &= ~0x01;
                                }
                            }
                            else if( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_ENDP )
                            {
                                if( (uint8_t)( USBFS_SetupReqValue & 0xFF ) == USB_REQ_FEAT_ENDP_HALT )
                                {
                                    switch( (uint8_t)( USBFS_SetupReqIndex & 0xFF ) )
                                    {

                                        case ( DEF_UEP_IN | DEF_UEP2 ):
                                            /* Set End-point 2 IN NAK */
                                            R8_UEP2_CTRL = ( R8_UEP2_CTRL & ~( RB_UEP_T_TOG | MASK_UEP_T_RES ) ) | UEP_T_RES_NAK;
                                            /* upload CSW */
                                            if( Udisk_Transfer_Status & DEF_UDISK_CSW_UP_FLAG )
                                            {
                                                UDISK_Up_CSW( );
                                            }
                                            break;
                                        
                                        case ( DEF_UEP_OUT | DEF_UEP3 ):
                                            /* Set End-point 3 OUT ACK */
                                            R8_UEP3_CTRL = ( R8_UEP3_CTRL & ~( RB_UEP_R_TOG | MASK_UEP_R_RES ) ) | UEP_R_RES_ACK;
                                            /* upload CSW */
                                            if( Udisk_Transfer_Status & DEF_UDISK_CSW_UP_FLAG )
                                            {
                                                UDISK_Up_CSW( );
                                            }
                                            break;

                                        default:
                                            errflag = 0xFF;
                                            break;
                                    }
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                            }
                            else
                            {
                                errflag = 0xFF;
                            }
                            break;

                        /* set or enable one usb feature */
                        case USB_SET_FEATURE:
                            if( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_DEVICE )
                            {
                                /* Set Device Feature */
                                if( (uint8_t)( USBFS_SetupReqValue & 0xFF ) == USB_REQ_FEAT_REMOTE_WAKEUP )
                                {
                                    if( MyCfgDescr[ 7 ] & 0x20 )
                                    {
                                        /* Set Wake-up flag, device prepare to sleep */
                                        USBFS_DevSleepStatus |= 0x01;
                                    }
                                    else
                                    {
                                        errflag = 0xFF;
                                    }
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                            }
                            else if( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_ENDP )
                            {
                                /* Set End-point Feature */
                                if( (uint8_t)( USBFS_SetupReqValue & 0xFF ) == USB_REQ_FEAT_ENDP_HALT )
                                {

                                    switch( (uint8_t)( USBFS_SetupReqIndex & 0xFF ) )
                                    {
                                        case ( DEF_UEP_IN | DEF_UEP2 ):
                                            R8_UEP2_CTRL = ( R8_UEP2_CTRL & ~( RB_UEP_T_TOG | MASK_UEP_T_RES ) ) | UEP_T_RES_STALL;
                                            break;
                                        
                                        case ( DEF_UEP_OUT | DEF_UEP3 ):
                                            R8_UEP3_CTRL = ( R8_UEP3_CTRL & ~( RB_UEP_R_TOG | MASK_UEP_R_RES ) ) | UEP_R_RES_STALL;
                                            break;

                                        default:
                                            errflag = 0xFF;
                                            break;
                                    }
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                            }
                            else
                            {
                                errflag = 0xFF;
                            }
                            break;

                        /* This request allows the host to select another setting for the specified interface  */
                        case USB_GET_INTERFACE:
                            USBFS_EP0_Buf[0] = 0x00;
                            if ( USBFS_SetupReqLen > 1 )
                            {
                                USBFS_SetupReqLen = 1;
                            }
                            break;

                        case USB_SET_INTERFACE:
                            break;

                        /* host get status of specified device/interface/end-points */
                        case USB_GET_STATUS:
                            USBFS_EP0_Buf[ 0 ] = 0x00;
                            USBFS_EP0_Buf[ 1 ] = 0x00;
                            if ( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_DEVICE )
                            {
                                if( USBFS_DevSleepStatus & 0x01 )
                                {
                                    USBFS_EP0_Buf[ 0 ] = 0x02;
                                }
                            }
                            else if( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_ENDP )
                            {
                                switch( (uint8_t)( USBFS_SetupReqIndex & 0xFF ) )
                                {
                                    case ( DEF_UEP_IN | DEF_UEP2 ):
                                        if( ( R8_UEP2_CTRL & MASK_UEP_T_RES ) == UEP_T_RES_STALL )
                                        {
                                            USBFS_EP0_Buf[ 0 ] = 0x01;
                                        }
                                        break;

                                    case ( DEF_UEP_OUT | DEF_UEP3 ):
                                        if( ( R8_UEP3_CTRL & MASK_UEP_R_RES ) == UEP_R_RES_STALL )
                                        {
                                            USBFS_EP0_Buf[ 0 ] = 0x01;
                                        }
                                        break;

                                    default:
                                        errflag = 0xFF;
                                        break;
                                }
                            }
                            else
                            {
                                errflag = 0xFF;
                            }

                            if ( USBFS_SetupReqLen > 2 )
                            {
                                USBFS_SetupReqLen = 2;
                            }
                            break;

                        default:
                            errflag = 0xFF;
                            break;
                    }
                }

                /* errflag = 0xFF means a request not support or some errors occurred, else correct */
                if( errflag == 0xFF)
                {
                    /* if one request not support, return stall */
                    R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;
                }
                else
                {
                    /* end-point 0 data Tx/Rx */
                    if( USBFS_SetupReqType & DEF_UEP_IN )
                    {
                        len = ( USBFS_SetupReqLen > DEF_USBD_UEP0_SIZE ) ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                        USBFS_SetupReqLen -= len;
                        R8_UEP0_T_LEN  = len;
                        R8_UEP0_CTRL = (R8_UEP0_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
                    }
                    else
                    {
                        if( USBFS_SetupReqLen == 0 )
                        {
                            R8_UEP0_T_LEN = 0;
                            R8_UEP0_CTRL = (R8_UEP0_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
                        }
                        else
                        {
                            R8_UEP0_CTRL = (R8_UEP0_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_ACK ;
                        }
                    }
                }
                break;

            /* Sof pack processing */
            case UIS_TOKEN_SOF:
                break;

            default :
                break;
        }
        R8_USB_INT_FG = RB_UIF_TRANSFER;
    }
    else if( intflag & RB_UIF_BUS_RST )
    {
        /* usb reset interrupt processing */
        USBFS_DevConfig = 0;
        USBFS_DevAddr = 0;
        USBFS_DevSleepStatus = 0;
        USBFS_DevEnumStatus = 0;

        R8_USB_DEV_AD = 0;
        USBFS_Device_Endp_Init( );
        R8_USB_INT_FG |= RB_UIF_BUS_RST;
    }
    else if( intflag & RB_UIF_SUSPEND )
    {
        /* usb suspend interrupt processing */
        R8_USB_INT_FG = RB_UIF_SUSPEND;
				Delay_Us(10);
        if ( R8_USB_MIS_ST & RB_UMS_SUSPEND )
        {
            USBFS_DevSleepStatus |= 0x02;
            if( USBFS_DevSleepStatus == 0x03 )
            {
                /* Handling usb sleep here */
            }
        }
        else
        {
            USBFS_DevSleepStatus &= ~0x02;
        }
    }
    else
    {
        /* other interrupts */
        R8_USB_INT_FG = intflag;
    }
}

/*********************************************************************
 * @fn      USBFS_Send_Resume
 *
 * @brief   USBFS device sends wake-up signal to host
 *
 * @return  none
 */
void USBFS_Send_Resume(void)
{
    R8_UDEV_CTRL ^= RB_UD_LOW_SPEED;
    Delay_Ms(8);
    R8_UDEV_CTRL ^= RB_UD_LOW_SPEED;
}
