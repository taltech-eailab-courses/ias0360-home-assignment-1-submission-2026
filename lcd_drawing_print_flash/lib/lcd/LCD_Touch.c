#include "LCD_Touch.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>


// ============================================================================
// Variables externes
// ============================================================================

extern LCD_DIS sLCD_DIS;

extern uint8_t id;


// ============================================================================
// Etat tactile
// ============================================================================

static TP_DEV sTP_DEV;
static TP_DRAW sTP_Draw;


// ============================================================================
// Données sauvegardées
// ============================================================================

static TP_DATA *tp_data = NULL;
static mutex_t *p_mutex = NULL;


// ============================================================================
// Shadow bitmap
// ============================================================================
//
// 1 octet par pixel.
//
// 0 = blanc / vide
// 1 = noir / dessiné
//
// Zone : 280 x 240 = 67200 octets
//
// Cette représentation est volontairement conservée pour ne pas changer
// toute la logique actuelle de dessin.
//

static uint8_t sDrawShadow[BOX_H][BOX_W];


// ============================================================================
// Etat sauvegarde
// ============================================================================
//
// false = aucune sauvegarde en cours
// true  = Core 1 est en train d'écrire le BMP
//
// Cette variable est partagée entre Core 0 et Core 1.
//

static volatile bool sSavePending = false;


// ============================================================================
// API état sauvegarde
// ============================================================================

bool TP_IsSavePending(void)
{
    return sSavePending;
}


void TP_SetSavePending(bool pending)
{
    sSavePending = pending;
}


// ============================================================================
// Dimensions sauvegarde
// ============================================================================

uint16_t TP_SavedWidth(void)
{
    return BOX_W;
}


uint16_t TP_SavedHeight(void)
{
    return BOX_H;
}


// ============================================================================
// Capture d'un pixel
// ============================================================================

static inline void Capture_SetPixel(
    uint16_t x,
    uint16_t y)
{
    if (x >= BOX_X0 &&
        x < BOX_X1 &&
        y >= BOX_Y0 &&
        y < BOX_Y1)
    {
        sDrawShadow[y - BOX_Y0][x - BOX_X0] = 1;
    }
}


// ============================================================================
// Lecture ADC du tactile
// ============================================================================

static uint16_t TP_Read_ADC(
    uint8_t CMD)
{
    uint16_t Data = 0;


    DEV_Digital_Write(
        TP_CS_PIN,
        0
    );


    SPI4W_Write_Byte(CMD);

    Driver_Delay_us(200);


    Data =
        SPI4W_Read_Byte(0x00);

    Data <<= 8;

    Data |=
        SPI4W_Read_Byte(0x00);

    Data >>= 3;


    DEV_Digital_Write(
        TP_CS_PIN,
        1
    );


    return Data;
}


// ============================================================================
// Moyenne ADC
// ============================================================================

#define READ_TIMES 5
#define LOST_NUM   1


static uint16_t TP_Read_ADC_Average(
    uint8_t Channel_Cmd)
{
    uint8_t i;
    uint8_t j;

    uint16_t Read_Buff[READ_TIMES];

    uint16_t Read_Sum = 0;
    uint16_t Read_Temp = 0;


    spi_set_baudrate(
        SPI_PORT,
        3000000
    );


    for (i = 0; i < READ_TIMES; i++)
    {
        Read_Buff[i] =
            TP_Read_ADC(
                Channel_Cmd
            );

        Driver_Delay_us(200);
    }


    spi_set_baudrate(
        SPI_PORT,
        18000000
    );


    // ------------------------------------------------------------------------
    // Tri
    // ------------------------------------------------------------------------

    for (i = 0;
         i < READ_TIMES - 1;
         i++)
    {
        for (j = i + 1;
             j < READ_TIMES;
             j++)
        {
            if (Read_Buff[i] >
                Read_Buff[j])
            {
                Read_Temp =
                    Read_Buff[i];

                Read_Buff[i] =
                    Read_Buff[j];

                Read_Buff[j] =
                    Read_Temp;
            }
        }
    }


    // ------------------------------------------------------------------------
    // Suppression des valeurs extrêmes
    // ------------------------------------------------------------------------

    for (i = LOST_NUM;
         i < READ_TIMES - LOST_NUM;
         i++)
    {
        Read_Sum +=
            Read_Buff[i];
    }


    Read_Temp =
        Read_Sum /
        (READ_TIMES - 2 * LOST_NUM);


    return Read_Temp;
}


// ============================================================================
// Lecture X/Y
// ============================================================================

static void TP_Read_ADC_XY(
    uint16_t *pXCh_Adc,
    uint16_t *pYCh_Adc)
{
    *pXCh_Adc =
        TP_Read_ADC_Average(
            0xD0
        );

    *pYCh_Adc =
        TP_Read_ADC_Average(
            0x90
        );
}


// ============================================================================
// Double lecture pour stabilité
// ============================================================================

#define ERR_RANGE 50


static bool TP_Read_TwiceADC(
    uint16_t *pXCh_Adc,
    uint16_t *pYCh_Adc)
{
    uint16_t XCh_Adc1;
    uint16_t YCh_Adc1;

    uint16_t XCh_Adc2;
    uint16_t YCh_Adc2;


    TP_Read_ADC_XY(
        &XCh_Adc1,
        &YCh_Adc1
    );


    Driver_Delay_us(10);


    TP_Read_ADC_XY(
        &XCh_Adc2,
        &YCh_Adc2
    );


    Driver_Delay_us(10);


    if (
        (
            (XCh_Adc2 <= XCh_Adc1 &&
             XCh_Adc1 < XCh_Adc2 + ERR_RANGE)
            ||
            (XCh_Adc1 <= XCh_Adc2 &&
             XCh_Adc2 < XCh_Adc1 + ERR_RANGE)
        )
        &&
        (
            (YCh_Adc2 <= YCh_Adc1 &&
             YCh_Adc1 < YCh_Adc2 + ERR_RANGE)
            ||
            (YCh_Adc1 <= YCh_Adc2 &&
             YCh_Adc2 < YCh_Adc1 + ERR_RANGE)
        )
    )
    {
        *pXCh_Adc =
            (XCh_Adc1 + XCh_Adc2) / 2;

        *pYCh_Adc =
            (YCh_Adc1 + YCh_Adc2) / 2;

        return true;
    }


    return false;
}


// ============================================================================
// Scan tactile
// ============================================================================

static uint8_t TP_Scan(
    uint8_t chCoordType)
{
    if (!DEV_Digital_Read(TP_IRQ_PIN))
    {
        // --------------------------------------------------------------------
        // Ecran touché
        // --------------------------------------------------------------------

        if (chCoordType)
        {
            TP_Read_TwiceADC(
                &sTP_DEV.Xpoint,
                &sTP_DEV.Ypoint
            );
        }
        else
        {
            if (TP_Read_TwiceADC(
                    &sTP_DEV.Xpoint,
                    &sTP_DEV.Ypoint))
            {
                // ------------------------------------------------------------
                // LCD 2.8"
                // ------------------------------------------------------------

                if (LCD_2_8 == id)
                {
                    sTP_Draw.Xpoint =
                        sLCD_DIS.LCD_Dis_Column
                        - sTP_DEV.fXfac *
                          sTP_DEV.Xpoint
                        - sTP_DEV.iXoff;

                    sTP_Draw.Ypoint =
                        sLCD_DIS.LCD_Dis_Page
                        - sTP_DEV.fYfac *
                          sTP_DEV.Ypoint
                        - sTP_DEV.iYoff;
                }

                // ------------------------------------------------------------
                // LCD 3.5"
                // ------------------------------------------------------------

                else
                {
                    if (sTP_DEV.TP_Scan_Dir ==
                        R2L_D2U)
                    {
                        sTP_Draw.Xpoint =
                            sTP_DEV.fXfac *
                            sTP_DEV.Xpoint
                            + sTP_DEV.iXoff;

                        sTP_Draw.Ypoint =
                            sTP_DEV.fYfac *
                            sTP_DEV.Ypoint
                            + sTP_DEV.iYoff;
                    }

                    else if (
                        sTP_DEV.TP_Scan_Dir ==
                        L2R_U2D)
                    {
                        sTP_Draw.Xpoint =
                            sLCD_DIS.LCD_Dis_Column
                            - sTP_DEV.fXfac *
                              sTP_DEV.Xpoint
                            - sTP_DEV.iXoff;

                        sTP_Draw.Ypoint =
                            sLCD_DIS.LCD_Dis_Page
                            - sTP_DEV.fYfac *
                              sTP_DEV.Ypoint
                            - sTP_DEV.iYoff;
                    }

                    else if (
                        sTP_DEV.TP_Scan_Dir ==
                        U2D_R2L)
                    {
                        sTP_Draw.Xpoint =
                            sTP_DEV.fXfac *
                            sTP_DEV.Ypoint
                            + sTP_DEV.iXoff;

                        sTP_Draw.Ypoint =
                            sTP_DEV.fYfac *
                            sTP_DEV.Xpoint
                            + sTP_DEV.iYoff;
                    }

                    else
                    {
                        sTP_Draw.Xpoint =
                            sLCD_DIS.LCD_Dis_Column
                            - sTP_DEV.fXfac *
                              sTP_DEV.Ypoint
                            - sTP_DEV.iXoff;

                        sTP_Draw.Ypoint =
                            sLCD_DIS.LCD_Dis_Page
                            - sTP_DEV.fYfac *
                              sTP_DEV.Xpoint
                            - sTP_DEV.iYoff;
                    }
                }
            }
        }


        // --------------------------------------------------------------------
        // Début d'appui
        // --------------------------------------------------------------------

        if (0 ==
            (sTP_DEV.chStatus &
             TP_PRESS_DOWN))
        {
            sTP_DEV.chStatus =
                TP_PRESS_DOWN |
                TP_PRESSED;

            sTP_DEV.Xpoint0 =
                sTP_DEV.Xpoint;

            sTP_DEV.Ypoint0 =
                sTP_DEV.Ypoint;
        }
    }

    else
    {
        // --------------------------------------------------------------------
        // Aucun contact
        // --------------------------------------------------------------------

        if (sTP_DEV.chStatus &
            TP_PRESS_DOWN)
        {
            sTP_DEV.chStatus &=
                ~(1 << 7);
        }
        else
        {
            sTP_DEV.Xpoint0 = 0;
            sTP_DEV.Ypoint0 = 0;

            sTP_DEV.Xpoint =
                0xffff;

            sTP_DEV.Ypoint =
                0xffff;
        }
    }


    return (
        sTP_DEV.chStatus &
        TP_PRESS_DOWN
    );
}


// ============================================================================
// Calibration / facteurs
// ============================================================================

void TP_GetAdFac(void)
{
    if (LCD_2_8 == id)
    {
        sTP_DEV.fXfac = 0.066626;
        sTP_DEV.fYfac = 0.089779;

        sTP_DEV.iXoff = -20;
        sTP_DEV.iYoff = -34;
    }
    else
    {
        if (sTP_DEV.TP_Scan_Dir ==
            D2U_L2R)
        {
            sTP_DEV.fXfac = -0.132443;
            sTP_DEV.fYfac = 0.089997;

            sTP_DEV.iXoff = 516;
            sTP_DEV.iYoff = -22;
        }

        else if (
            sTP_DEV.TP_Scan_Dir ==
            L2R_U2D)
        {
            sTP_DEV.fXfac = 0.089697;
            sTP_DEV.fYfac = 0.134792;

            sTP_DEV.iXoff = -21;
            sTP_DEV.iYoff = -39;
        }

        else if (
            sTP_DEV.TP_Scan_Dir ==
            R2L_D2U)
        {
            sTP_DEV.fXfac = 0.089915;
            sTP_DEV.fYfac = 0.133178;

            sTP_DEV.iXoff = -22;
            sTP_DEV.iYoff = -38;
        }

        else if (
            sTP_DEV.TP_Scan_Dir ==
            U2D_R2L)
        {
            sTP_DEV.fXfac = -0.132906;
            sTP_DEV.fYfac = 0.087964;

            sTP_DEV.iXoff = 517;
            sTP_DEV.iYoff = -20;
        }

        else
        {
            LCD_Clear(
                LCD_BACKGROUND
            );

            GUI_DisString_EN(
                0,
                60,
                "Does not support touch-screen "
                "calibration in this direction",
                &Font16,
                FONT_BACKGROUND,
                RED
            );
        }
    }
}


// ============================================================================
// Affichage de la boîte de dessin
// ============================================================================

void TP_Dialog(void)
{
    LCD_Clear(
        LCD_BACKGROUND
    );


    // ------------------------------------------------------------------------
    // Bouton CLEAR
    // ------------------------------------------------------------------------

    GUI_DisString_EN(
        sLCD_DIS.LCD_Dis_Column - 60,
        0,
        "CLEAR",
        &Font16,
        BLACK,
        WHITE
    );


    // ------------------------------------------------------------------------
    // Bouton SAVE
    // ------------------------------------------------------------------------

    GUI_DisString_EN(
        sLCD_DIS.LCD_Dis_Column - 120,
        0,
        "SAVE",
        &Font16,
        BLACK,
        WHITE
    );


    // ------------------------------------------------------------------------
    // Zone de dessin
    // ------------------------------------------------------------------------

    GUI_DrawRectangle(
        BOX_X0,
        BOX_Y0,
        BOX_X1,
        BOX_Y1,
        BLACK,
        DRAW_EMPTY,
        DOT_PIXEL_2X2
    );


    // ------------------------------------------------------------------------
    // Effacement de la shadow bitmap
    // ------------------------------------------------------------------------

    memset(
        sDrawShadow,
        0,
        sizeof(sDrawShadow)
    );
}


// ============================================================================
// Sauvegarde
// ============================================================================
//
// Cette fonction ne fabrique pas directement le BMP.
//
// Elle :
//
//     1. vérifie qu'une sauvegarde n'est pas déjà en cours
//     2. copie le shadow bitmap dans tp_data
//     3. signale Core 1
//
// Core 1 fabrique ensuite le BMP sur la SD.
//

void TP_Save(void)
{
    // ------------------------------------------------------------------------
    // Une sauvegarde est-elle déjà en cours ?
    // ------------------------------------------------------------------------

    if (TP_IsSavePending())
    {
        printf(
            "TP_Save: save already pending.\n"
        );

        return;
    }


    // ------------------------------------------------------------------------
    // Vérification des pointeurs
    // ------------------------------------------------------------------------

    if (!tp_data ||
        !p_mutex)
    {
        printf(
            "TP_Save: tp_data or mutex not initialized.\n"
        );

        return;
    }


    printf(
        "TP_Save: preparing %ux%u bitmap.\n",
        (unsigned)BOX_W,
        (unsigned)BOX_H
    );


    // ------------------------------------------------------------------------
    // Protection de tp_data
    // ------------------------------------------------------------------------

    mutex_enter_blocking(
        p_mutex
    );


    // ------------------------------------------------------------------------
    // Libération d'une ancienne donnée
    // ------------------------------------------------------------------------

    if (tp_data->data)
    {
        free(
            tp_data->data
        );

        tp_data->data = NULL;
        tp_data->data_len = 0;
    }


    // ------------------------------------------------------------------------
    // Taille de la copie
    // ------------------------------------------------------------------------

    tp_data->data_len =
        (size_t)BOX_W *
        (size_t)BOX_H;


    // ------------------------------------------------------------------------
    // Allocation
    // ------------------------------------------------------------------------

    tp_data->data =
        (uint8_t *)malloc(
            tp_data->data_len
        );


    // ------------------------------------------------------------------------
    // IMPORTANT :
    //
    // En cas d'échec malloc(), il faut impérativement libérer le mutex.
    // ------------------------------------------------------------------------

    if (!tp_data->data)
    {
        printf(
            "TP_Save: malloc(%u) failed.\n",
            (unsigned)tp_data->data_len
        );

        tp_data->data_len = 0;

        mutex_exit(
            p_mutex
        );

        return;
    }


    // ------------------------------------------------------------------------
    // Copie du dessin
    // ------------------------------------------------------------------------

    memcpy(
        tp_data->data,
        sDrawShadow,
        tp_data->data_len
    );

    printf(
        "TP_Save: bitmap copied: %zu bytes.\n",
        tp_data->data_len
    );


    // ------------------------------------------------------------------------
    // Fin protection tp_data
    // ------------------------------------------------------------------------

    mutex_exit(
        p_mutex
    );


    // ------------------------------------------------------------------------
    // Signaler qu'une sauvegarde est en cours
    // ------------------------------------------------------------------------
    //
    // A partir de maintenant, Core 0 ne doit plus utiliser le SPI.
    //

    TP_SetSavePending(
        true
    );


    // ------------------------------------------------------------------------
    // Demande à Core 1
    // ------------------------------------------------------------------------

    multicore_fifo_push_blocking(
        DATA_READY_FLAG
    );


    printf(
        "TP_Save: request sent to Core 1.\n"
    );
}


// ============================================================================
// Gestion du dessin
// ============================================================================

void TP_DrawBoard(void)
{
    // ------------------------------------------------------------------------
    // Scanner le tactile
    // ------------------------------------------------------------------------

    TP_Scan(0);


    if (!(sTP_DEV.chStatus &
          TP_PRESS_DOWN))
    {
        // Rien à dessiner
        SPI4W_Write_Byte(0xFF);
        return;
    }


    // ------------------------------------------------------------------------
    // Vitesse SPI utilisée pour LCD
    // ------------------------------------------------------------------------

    spi_set_baudrate(
        SPI_PORT,
        10000000
    );


    // ------------------------------------------------------------------------
    // Bouton CLEAR
    // ------------------------------------------------------------------------

    if (
        sTP_Draw.Xpoint >
            (sLCD_DIS.LCD_Dis_Column - 60)
        &&
        sTP_Draw.Ypoint < 16
    )
    {
        TP_Dialog();

        spi_set_baudrate(
            SPI_PORT,
            5000000
        );

        // Très important :
        // ne pas continuer dans la partie "dessin".
        SPI4W_Write_Byte(0xFF);

        return;
    }


    // ------------------------------------------------------------------------
    // Bouton SAVE
    // ------------------------------------------------------------------------

    if (
        sTP_Draw.Xpoint >
            (sLCD_DIS.LCD_Dis_Column - 120)
        &&
        sTP_Draw.Xpoint <
            (sLCD_DIS.LCD_Dis_Column - 80)
        &&
        sTP_Draw.Ypoint < 24
    )
    {
        /*
        * Effacer SAVE AVANT de réveiller Core 1.
        *
        * Important : après TP_Save(), Core 1 peut commencer
        * immédiatement à utiliser le SPI pour la SD.
        */
        TP_HideSaveButton();

        TP_Save();

        /*
        * Si TP_Save() n'a finalement pas lancé de sauvegarde
        * (malloc échoué, etc.), remettre immédiatement SAVE.
        */
        if (!TP_IsSavePending())
        {
            TP_ShowSaveButton();
        }

        spi_set_baudrate(
            SPI_PORT,
            5000000
        );

        SPI4W_Write_Byte(0xFF);

        return;
    }


    // ------------------------------------------------------------------------
    // Couleur du stylo
    // ------------------------------------------------------------------------

    sTP_Draw.Color =
        BLACK;


    // ------------------------------------------------------------------------
    // Zone de dessin
    // ------------------------------------------------------------------------
    //
    // On laisse une marge d'un pixel afin que les points +1 ne dépassent
    // pas de la zone capturée.
    //

    if  (
        sTP_Draw.Xpoint >= BOX_X0 &&
        sTP_Draw.Xpoint < (BOX_X1 - 1) &&
        sTP_Draw.Ypoint >= BOX_Y0 &&
        sTP_Draw.Ypoint < (BOX_Y1 - 1)
    )
    {
        // --------------------------------------------------------------------
        // Point 1
        // --------------------------------------------------------------------

        GUI_DrawPoint(
            sTP_Draw.Xpoint,
            sTP_Draw.Ypoint,
            sTP_Draw.Color,
            DOT_PIXEL_1X1,
            DOT_FILL_RIGHTUP
        );

        Capture_SetPixel(
            sTP_Draw.Xpoint,
            sTP_Draw.Ypoint
        );


        // --------------------------------------------------------------------
        // Point 2
        // --------------------------------------------------------------------

        GUI_DrawPoint(
            sTP_Draw.Xpoint + 1,
            sTP_Draw.Ypoint,
            sTP_Draw.Color,
            DOT_PIXEL_1X1,
            DOT_FILL_RIGHTUP
        );

        Capture_SetPixel(
            sTP_Draw.Xpoint + 1,
            sTP_Draw.Ypoint
        );


        // --------------------------------------------------------------------
        // Point 3
        // --------------------------------------------------------------------

        GUI_DrawPoint(
            sTP_Draw.Xpoint,
            sTP_Draw.Ypoint + 1,
            sTP_Draw.Color,
            DOT_PIXEL_1X1,
            DOT_FILL_RIGHTUP
        );

        Capture_SetPixel(
            sTP_Draw.Xpoint,
            sTP_Draw.Ypoint + 1
        );


        // --------------------------------------------------------------------
        // Point 4
        // --------------------------------------------------------------------

        GUI_DrawPoint(
            sTP_Draw.Xpoint + 1,
            sTP_Draw.Ypoint + 1,
            sTP_Draw.Color,
            DOT_PIXEL_1X1,
            DOT_FILL_RIGHTUP
        );

        Capture_SetPixel(
            sTP_Draw.Xpoint + 1,
            sTP_Draw.Ypoint + 1
        );


        // --------------------------------------------------------------------
        // Renforcement du trait
        // --------------------------------------------------------------------

        GUI_DrawPoint(
            sTP_Draw.Xpoint,
            sTP_Draw.Ypoint,
            sTP_Draw.Color,
            DOT_PIXEL_2X2,
            DOT_FILL_RIGHTUP
        );
    }


    // ------------------------------------------------------------------------
    // Retour à une vitesse SPI plus basse
    // ------------------------------------------------------------------------

    spi_set_baudrate(
        SPI_PORT,
        5000000
    );


    SPI4W_Write_Byte(
        0xFF
    );
}


// ============================================================================
// Initialisation tactile
// ============================================================================

void TP_Init(
    LCD_SCAN_DIR Lcd_ScanDir,
    TP_DATA *tp_data_ptr,
    mutex_t *mutex)
{
    DEV_Digital_Write(
        TP_CS_PIN,
        1
    );


    sTP_DEV.TP_Scan_Dir =
        Lcd_ScanDir;


    tp_data =
        tp_data_ptr;


    p_mutex =
        mutex;


    TP_Read_ADC_XY(
        &sTP_DEV.Xpoint,
        &sTP_DEV.Ypoint
    );
}


// ============================================================================
// SAVE button modification while saving
// ============================================================================

void TP_HideSaveButton(void)
{
    GUI_DisString_EN(
        sLCD_DIS.LCD_Dis_Column - 120,
        0,
        "    ",
        &Font16,
        BLACK,
        WHITE
    );
}


void TP_ShowSaveButton(void)
{
    GUI_DisString_EN(
        sLCD_DIS.LCD_Dis_Column - 120,
        0,
        "SAVE",
        &Font16,
        BLACK,
        WHITE
    );
}
