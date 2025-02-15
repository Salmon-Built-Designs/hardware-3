/*++

Copyright (c) 2010 Evan Green

Module Name:

    winmain.c

Abstract:

    This module implements the hardware layer for the main board on Windows,
    used for testing.

Author:

    Evan Green 5-Nov-2010

Environment:

    Windows

--*/

//
// ------------------------------------------------------------------- Includes
//

#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>

#include "types.h"
#include "mainboard.h"
#include "fontdata.h"

//
// ---------------------------------------------------------------- Definitions
//

#define APPLICATION_NAME "Main Board Simulator"

//
// Define the periodic timer rate to be 1 millisecond, the fastest rate Windows
// supports.
//

#define TIMER_RATE_MS 1

//
// Define the color of a pixel when it's off.
//

#define INTENSITY_OFF 0x50
#define OFF_COLOR RGB(INTENSITY_OFF, INTENSITY_OFF, INTENSITY_OFF)

//
// Define how big a pixel should look.
//

#define MATRIX_PIXEL_WIDTH 16

//
// Define how big a trackball should look.
//

#define TRACKBALL_WIDTH 24

//
// Define how big the standby LED should look.
//

#define STANDBY_LED_WIDTH 16

//
// Define the spacing between pixels.
//

#define MATRIX_PIXEL_SPACING 20

//
// Define the dimensions of the LED Matrix.
//

#define MATRIX_SCREEN_HEIGHT ((MATRIX_HEIGHT + 3) * MATRIX_PIXEL_SPACING)
#define MATRIX_SCREEN_WIDTH ((MATRIX_WIDTH + 2) * MATRIX_PIXEL_SPACING)

//
// Define the width and height of the LCD control.
//

#define LCD_WIDTH 200
#define LCD_HEIGHT 40

//
// Define how much vertical space should separate the LCD from other elements.
//

#define LCD_Y_PADDING 2

//
// Define the top left corner of the LCD control.
//

#define LCD_X ((MATRIX_SCREEN_WIDTH / 2) - (LCD_WIDTH / 2))
#define LCD_Y (MATRIX_SCREEN_HEIGHT - LCD_HEIGHT + LCD_Y_PADDING)
#define LCD_SCREEN_HEIGHT (LCD_HEIGHT + (2 * LCD_Y_PADDING))

//
// Define the trackball/standby light positions.
//

#define TRACKBALL_X_PADDING 175
#define TRACKBALL_Y_PADDING MATRIX_PIXEL_SPACING
#define TRACKBALL_Y (MATRIX_SCREEN_HEIGHT + LCD_SCREEN_HEIGHT)
#define TRACKBALL1_X ((MATRIX_SCREEN_WIDTH / 2) - TRACKBALL_X_PADDING)
#define TRACKBALL2_X ((MATRIX_SCREEN_WIDTH / 2) + TRACKBALL_X_PADDING)
#define STANDBY_X (MATRIX_SCREEN_WIDTH / 2)
#define TRACKBALL_SCREEN_HEIGHT (MATRIX_PIXEL_SPACING + 2 * TRACKBALL_Y_PADDING)

//
// Define LCD parameters.
//

#define LCD_FONT_NAME "Courier New"
#define LCD_FOREGROUND RGB(0xC0, 0xC0, 0xFF)
#define LCD_BACKGROUND RGB(0x20, 0x20, 0x25)

//
// Define the total window size.
//

#define WINDOW_HEIGHT \
        (MATRIX_SCREEN_HEIGHT + LCD_SCREEN_HEIGHT + TRACKBALL_SCREEN_HEIGHT)

#define WINDOW_WIDTH MATRIX_SCREEN_WIDTH

//
// ----------------------------------------------- Internal Function Prototypes
//

DWORD
WINAPI
HlpUiThreadMain (
    LPVOID Parameter
    );

LRESULT
WINAPI
HlpWindowProcedure (
    HWND hWnd,
    UINT Message,
    WPARAM wParam,
    LPARAM lParam
    );

VOID
CALLBACK
HlpTimerService (
    UINT TimerId,
    UINT Message,
    DWORD_PTR User,
    DWORD_PTR Parameter1,
    DWORD_PTR Parameter2
    );

BOOL
HlpIsMatrixStale (
    );

VOID
HlpRedrawMatrix (
    HDC Dc
    );

VOID
HlpRedrawLcd (
    HDC Dc
    );

COLORREF
HlpPixelToColorRef (
    USHORT Pixel
    );

BOOLEAN
HlpInitializeLcd (
    HWND Window
    );

BOOLEAN
HlpProcessInputs (
    ULONG InputKey,
    BOOLEAN KeyDown
    );

//
// -------------------------------------------------------------------- Globals
//

//
// Store the application instance handle.
//

HANDLE HlInstance;

//
// Store a handle to the main clock tick timer.
//

MMRESULT HlTimer = 0;

//
// Store a handle to the main window.
//

HWND HlWindow;

//
// Store resources used for updating the LCD. Use an extra character so the
// string is always null terminated.
//

HFONT HlLcdFont;
CHAR HlLcdLine1[LCD_LINE_LENGTH + 1];
CHAR HlLcdLine2[LCD_LINE_LENGTH + 1];
CHAR HlLcdCurrentAddress = 0;

//
// Maintain a shadow copy of the current state of the screen.
//

USHORT HlMatrix[MATRIX_HEIGHT][MATRIX_WIDTH];

//
// Maintain which pixels have text printed on them.
//

USHORT HlTextColor[MATRIX_HEIGHT][MATRIX_WIDTH];
BOOLEAN HlNewTextPrinted;

//
// Maintain a shadow copy of the current state of the trackballs/standby light.
//

USHORT HlTrackball1;
USHORT HlTrackball2;
USHORT HlWhiteLeds;

//
// Define a shadow copy of the inputs.
//

USHORT HlRawInputs;
USHORT HlInputEdges;

//
// Remember the value of QPC when the app started.
//

ULONGLONG HlInitialQpcValue;
ULONGLONG HlLastTime;

//
// ------------------------------------------------------ Data Type Definitions
//

//
// ------------------------------------------------------------------ Functions
//

INT
WINAPI
WinMain (
    HINSTANCE hInstance,
    HINSTANCE hPrevInst,
    LPSTR lpszCmdParam,
    INT nCmdShow
    )

/*++

Routine Description:

    This routine is the main entry point for a Win32 application.

Arguments:

    hInstance - Supplies a handle to the current instance of the application.

    hPrevInstance - Supplies a handle to the previous instance of the
        application.

    lpCmdLine - Supplies a pointer to a null-terminated string specifying the
        command line for the application, excluding the program name.

    nCmdShow - Specifies how the window is to be shown.

Return Value:

    Returns TRUE on success, FALSE on failure.

--*/

{

    HlInstance = hInstance;
    main();
    return 0;
}

VOID
HlInitialize (
    VOID
    )

/*++

Routine Description:

    This routine initializes the hardware abstraction layer.

Arguments:

    None.

Return Value:

    None.

--*/

{

    LARGE_INTEGER LargeInteger;
    BOOLEAN Result;
    SYSTEMTIME SystemTime;
    HANDLE UiThread;

    QueryPerformanceCounter(&LargeInteger);
    HlInitialQpcValue = LargeInteger.QuadPart;
    srand(time(NULL));

    //
    // Set up the current time variables.
    //

    GetLocalTime(&SystemTime);
    KeCurrentMonth = SystemTime.wMonth - 1;
    KeCurrentWeekday = SystemTime.wDayOfWeek;
    KeCurrentDate = SystemTime.wDay - 1;
    KeCurrentHours = SystemTime.wHour;
    KeCurrentMinutes = SystemTime.wMinute;
    KeCurrentHalfSeconds = SystemTime.wSecond * 2;

    //
    // Kick off the UI thread.
    //

    UiThread = CreateThread(NULL, 0, HlpUiThreadMain, NULL, 0, NULL);
    if (UiThread == NULL) {
        Result = FALSE;
        goto InitializeEnd;
    }

    Result = TRUE;

InitializeEnd:
    return;
}

VOID
HlClearLcdScreen (
    VOID
    )

/*++

Routine Description:

    This routine clears the LCD screen.

Arguments:

    None.

Return Value:

    None.

--*/

{

    memset(HlLcdLine1, ' ', sizeof(HlLcdLine1) - 1);
    memset(HlLcdLine2, ' ', sizeof(HlLcdLine2) - 1);
    HlLcdLine1[LCD_LINE_LENGTH] = '\0';
    HlLcdLine2[LCD_LINE_LENGTH] = '\0';
    InvalidateRect(HlWindow, NULL, TRUE);
    UpdateWindow(HlWindow);
    return;
}

VOID
HlSetLcdAddress (
    UCHAR Address
    )

/*++

Routine Description:

    This routine sets the address of the next character to be written to the
    LCD screen.

Arguments:

    Address - Supplies the address to write.

Return Value:

    None.

--*/

{

    HlLcdCurrentAddress = Address;
}

VOID
HlLcdPrintStringFromFlash (
    PPGM String
    )

/*++

Routine Description:

    This routine prints a string at the current LCD address. Wrapping to the
    next line is not accounted for.

Arguments:

    String - Supplies a pointer to an address in code space of the string to
        print.

Return Value:

    None.

--*/

{

    HlLcdPrintString((PUCHAR)String);
    return;
}

VOID
HlLcdPrintString (
    PCHAR String
    )

/*++

Routine Description:

    This routine prints a string at the current LCD address. Wrapping to the
    next line is not accounted for.

Arguments:

    String - Supplies a pointer to an address in data space of the string to
        print.

Return Value:

    None.

--*/

{

    PUCHAR Address;
    UCHAR Length;
    UCHAR Offset;

    Address = (PUCHAR)&HlLcdLine1;
    if (HlLcdCurrentAddress >= LCD_SECOND_LINE) {
        Address = HlLcdLine2;
    }

    Offset = HlLcdCurrentAddress & LCD_LINE_OFFSET_MASK;
    Length = strlen(String);
    if (Offset + Length > LCD_LINE_LENGTH) {

        assert(FALSE);

        Length = LCD_LINE_LENGTH - Offset;
    }

    memcpy(Address + Offset, String, Length);
    InvalidateRect(HlWindow, NULL, TRUE);
    UpdateWindow(HlWindow);
    return;
}

USHORT
HlRandom (
    VOID
    )

/*++

Routine Description:

    This routine returns a random number between 0 and 65535.

Arguments:

    None.

Return Value:

    Returns a random number between 0 and 65535.

--*/

{

    return (USHORT)rand();
}

VOID
HlPrintText (
    UCHAR Size,
    UCHAR XPosition,
    UCHAR YPosition,
    UCHAR Character,
    USHORT Color
    )

/*++

Routine Description:

    This routine prints a character onto the matrix.

Arguments:

    Size - Supplies the size of the character to print. Valid values are as
        follows:

        0 - Prints a 3 x 5 character.

        1 - Prints a 5 x 7 character.

    XPosition - Supplies the X coordinate of the upper left corner of the
        letter.

    YPosition - Supplies the Y coordinate of the upper left corner of the
        letter.

    Character - Supplies the character to print,

    Color - Supplies the color to print the character.

Return Value:

    None.

--*/

{

    UCHAR BitSet;
    UCHAR Column;
    UCHAR EncodedData;
    UCHAR XPixel;
    UCHAR YPixel;

    switch (Size) {
        case 0:

            //
            // Not all characters are printable, but print the ones that are.
            //

            if ((Character >= '0') && (Character <= '9')) {
                Character = FONT_3X5_NUMERIC_OFFSET + (Character - '0');

            } else if (Character == ':') {
                Character = FONT_3X5_COLON_OFFSET;

            } else if (Character == '=') {
                Character = FONT_3X5_EQUALS_OFFSET;

            } else if ((Character >= 'a') && (Character <= 'z')) {
                Character = FONT_3X5_ALPHA_OFFSET + Character - 'a';

            } else if ((Character >= 'A') && (Character <= 'Z')) {
                Character = FONT_3X5_ALPHA_OFFSET + Character - 'A';

            } else {
                Character = FONT_3X5_SPACE_OFFSET;
            }

            //
            // Loop over every destination column.
            //

            for (XPixel = XPosition;
                 ((XPixel < XPosition + 3) && (XPixel < MATRIX_WIDTH));
                 XPixel += 1) {

                //
                // Loop over every destination row. The encoded bytes are laid
                // out as follows:
                //
                // -----*** **+++++
                // ABCDEABC DEABCDE0
                //
                // Where - is column 0, * is column 1, and + is column 2. A-F
                // represent rows 0-4.
                //

                for (YPixel = YPosition;
                     ((YPixel < YPosition + 5) && (YPixel < MATRIX_HEIGHT));
                     YPixel += 1) {

                    BitSet = FALSE;
                    Column = YPixel - YPosition;

                    //
                    // In column 0, the rows are simply packed into the high 5
                    // bits of the first byte.
                    //

                    if (XPixel - XPosition == 0) {
                        if ((KeFontData3x5[Character][0] &
                             (1 << (7 - Column))) != 0) {

                            BitSet = TRUE;
                        }

                    //
                    // In column 1, the first 3 rows are packed into the low
                    // 3 bits of the first byte. The other 2 rows are packed
                    // into the high 2 bits of the second byte.
                    //

                    } else if (XPixel - XPosition == 1) {
                        if (Column < 3) {
                            if ((KeFontData3x5[Character][0] &
                                 (1 << (2 - Column))) != 0) {

                                BitSet = TRUE;
                            }

                        } else {
                            if ((KeFontData3x5[Character][1] &
                                 (1 << (7 - (Column - 3)))) != 0) {

                                BitSet = TRUE;
                            }
                        }

                    //
                    // The third column of data is packed into the remaining
                    // bits of byte two, with the lowest bit going unused.
                    //

                    } else {
                        if ((KeFontData3x5[Character][1] &
                             (1 << (5 - Column))) != 0) {

                            BitSet = TRUE;
                        }
                    }

                    if (BitSet != FALSE) {
                        HlTextColor[YPixel][XPixel] = Color;

                    } else {
                        HlTextColor[YPixel][XPixel] = 0;
                    }
                }
            }

            break;

        case 1:
        default:
            for (XPixel = XPosition;
                 ((XPixel < XPosition + 5) && (XPixel < MATRIX_WIDTH));
                 XPixel += 1) {

                EncodedData = KeFontData5x7[Character][XPixel - XPosition];
                for (YPixel = YPosition;
                     ((YPixel < YPosition + 8) && (YPixel < MATRIX_HEIGHT));
                     YPixel += 1) {

                    if ((EncodedData & 0x1) != 0) {
                        HlTextColor[YPixel][XPixel] = Color;

                    } else {
                        HlTextColor[YPixel][XPixel] = 0;
                    }

                    EncodedData = EncodedData >> 1;

                }
            }

            break;
    }

    HlNewTextPrinted = TRUE;
    return;
}

VOID
HlClearScreen (
    VOID
    )

/*++

Routine Description:

    This routine clears the entire screen, turning off all LEDs.

Arguments:

    None.

Return Value:

    None.

--*/

{

    ULONG XPixel;
    ULONG YPixel;

    for (YPixel = 0; YPixel < MATRIX_HEIGHT; YPixel += 1) {
        for (XPixel = 0; XPixel < MATRIX_WIDTH; XPixel += 1) {
            HlTextColor[YPixel][XPixel] = 0;
        }
    }

    HlNewTextPrinted = TRUE;
    return;
}

VOID
HlUpdateDisplay (
    VOID
    )

/*++

Routine Description:

    This routine allows the hardware layer to update the matrix display.

Arguments:

    None.

Return Value:

    None.

--*/

{

    //
    // Refresh the screen if needed.
    //

    if (HlpIsMatrixStale() != FALSE) {
        InvalidateRect(HlWindow, NULL, FALSE);
        UpdateWindow(HlWindow);
    }

    return;
}

//
// --------------------------------------------------------- Internal Functions
//

DWORD
WINAPI
HlpUiThreadMain (
    LPVOID Parameter
    )

/*++

Routine Description:

    This routine is the startup routine for the UI thread.

Arguments:

    Parameter - Unused parameter.

Return Value:

    Returns 0 on success, or nonzero if there was an error.

--*/

{

    WNDCLASSEX Class;
    BOOLEAN ClassRegistered;
    BOOLEAN Result;
    ULONG WindowHeight;
    MSG WindowMessage;
    ULONG WindowWidth;

    ClassRegistered = FALSE;
    WindowWidth = WINDOW_WIDTH;
    WindowHeight = WINDOW_HEIGHT;

    //
    // Register the window class.
    //

    Class.cbSize = sizeof(WNDCLASSEX);
    Class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    Class.lpfnWndProc = HlpWindowProcedure;
    Class.cbClsExtra = 0;
    Class.cbWndExtra = 0;
    Class.hInstance = HlInstance;
    Class.hIcon = NULL;
    Class.hIconSm = NULL;
    Class.hCursor = LoadCursor(NULL, IDC_ARROW);
    Class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    Class.lpszMenuName = NULL;
    Class.lpszClassName = APPLICATION_NAME;
    Result = RegisterClassEx(&Class);
    if (Result == FALSE) {
        goto UiThreadMainEnd;
    }

    ClassRegistered = TRUE;

    //
    // Create the UI window.
    //

    HlWindow = CreateWindowEx(WS_EX_CLIENTEDGE,
                              APPLICATION_NAME,
                              APPLICATION_NAME,
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_POPUP,
                              0,
                              0,
                              WindowWidth,
                              WindowHeight,
                              NULL,
                              NULL,
                              HlInstance,
                              NULL);

    if (HlWindow == NULL) {
        Result = FALSE;
        goto UiThreadMainEnd;
    }

    SetFocus(HlWindow);
    InvalidateRect(HlWindow, NULL, TRUE);
    UpdateWindow(HlWindow);

    //
    // Kick off the periodic timer.
    //

    timeBeginPeriod(1);
    HlTimer = timeSetEvent(TIMER_RATE_MS,
                           TIMER_RATE_MS,
                           HlpTimerService,
                           TIMER_RATE_MS * 1000,
                           TIME_PERIODIC | TIME_CALLBACK_FUNCTION);

    if (HlTimer == 0) {
        Result = FALSE;
        goto UiThreadMainEnd;
    }

    //
    // Dispatch pending messages to the window.
    //

    while (GetMessage(&WindowMessage, NULL, 0, 0) != FALSE) {
        if (WindowMessage.message == WM_QUIT) {
            UnregisterClass(APPLICATION_NAME, HlInstance);
            exit(0);
        }

        TranslateMessage(&WindowMessage);
        DispatchMessage(&WindowMessage);
    }

UiThreadMainEnd:
    if (ClassRegistered == FALSE) {
        UnregisterClass(APPLICATION_NAME, HlInstance);
    }

    if (HlTimer != 0) {
        timeKillEvent(HlTimer);
    }

    timeEndPeriod(1);
    exit(0);
    return 0;
}

LRESULT
WINAPI
HlpWindowProcedure (
    HWND hWnd,
    UINT Message,
    WPARAM wParam,
    LPARAM lParam
    )

/*++

Routine Description:

    This routine is the main message pump for the main board window. It
    receives messages pertaining to the window and handles interesting ones.

Arguments:

    hWnd - Supplies the handle for the overall window.

    Message - Supplies the message being sent to the window.

    WParam - Supplies the "width" parameter, basically the first parameter of
        the message.

    LParam - Supplies the "length" parameter, basically the second parameter of
        the message.

Return Value:

    Returns FALSE if the message was handled, or TRUE if the message was not
    handled and the default handler should be invoked.

--*/

{

    HDC Dc;
    BOOLEAN Handled;
    BOOLEAN KeyDown;
    PAINTSTRUCT PaintStructure;
    BOOLEAN Result;

    KeyDown = FALSE;
    switch (Message) {
    case WM_CREATE:
        Result = HlpInitializeLcd(hWnd);
        if (Result == FALSE) {
            MessageBox(NULL, "Unable to initialize LCD.", "Error", MB_OK);
            PostQuitMessage(0);
        }

        break;

    case WM_PAINT:
        Dc = BeginPaint(hWnd, &PaintStructure);
        HlpRedrawMatrix(Dc);
        EndPaint(hWnd, &PaintStructure);
        break;

    case WM_KEYDOWN:
        KeyDown = TRUE;

        //
        // Fall through.
        //

    case WM_KEYUP:
        Handled = HlpProcessInputs(wParam, KeyDown);
        if (Handled == FALSE) {
            return DefWindowProc(hWnd, Message, wParam, lParam);
        }

        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    default:
        return DefWindowProc(hWnd, Message, wParam, lParam);
    }

    return FALSE;
}

VOID
CALLBACK
HlpTimerService (
    UINT TimerId,
    UINT Message,
    DWORD_PTR User,
    DWORD_PTR Parameter1,
    DWORD_PTR Parameter2
    )

/*++

Routine Description:

    This routine is the callback from the timer.

Arguments:

    TimerId - Supplies the timer ID that fired. Unused.

    Message - Supplies the timer message. Unused.

    User - Supplies the user paramater. In this case this is the period of the
        timer.

    Parameter1 - Supplies an unused parameter.

    Parameter2 - Supplies an unused parameter.

Return Value:

    None.

--*/

{

    LARGE_INTEGER CurrentQpcTime;
    ULONGLONG CurrentTime;
    LARGE_INTEGER QpcFrequency;
    ULONG TimeDifference;

    QueryPerformanceCounter(&CurrentQpcTime);
    QueryPerformanceFrequency(&QpcFrequency);
    CurrentTime = (CurrentQpcTime.QuadPart - HlInitialQpcValue) *
                  1000 * 32 / QpcFrequency.QuadPart;

    //
    // Update system time.
    //

    TimeDifference = (ULONG)(CurrentTime - HlLastTime);
    KeUpdateTime((USHORT)TimeDifference);
    HlLastTime = CurrentTime;

    //
    // Update the inputs.
    //

    KeRawInputs = HlRawInputs;
    KeInputEdges |= HlInputEdges;
    HlInputEdges = 0;
    return;
}

BOOL
HlpIsMatrixStale (
    )

/*++

Routine Description:

    This routine determines whether the output matrix needs to be redrawn.

Arguments:

    None.

Return Value:

    TRUE if the matrix needs to be redrawn.

    FALSE if the matrix is up to date.

--*/

{

    BOOL Changes;
    USHORT Pixel;
    ULONG XPixel;
    ULONG YPixel;

    Changes = FALSE;
    if (HlNewTextPrinted != FALSE) {
        HlNewTextPrinted = FALSE;
        return TRUE;
    }

    for (YPixel = 0; YPixel < MATRIX_HEIGHT; YPixel += 1) {
        for (XPixel = 0; XPixel < MATRIX_WIDTH; XPixel += 1) {
            Pixel = KeMatrix[YPixel][XPixel];
            if (Pixel != HlMatrix[YPixel][XPixel]) {
                Changes = TRUE;
                break;
            }
        }
    }

    if ((HlTrackball1 != KeTrackball1) ||
        (HlTrackball2 != KeTrackball2) ||
        (HlWhiteLeds != KeWhiteLeds)) {

        Changes = TRUE;
    }

    return Changes;
}

VOID
HlpRedrawMatrix (
    HDC Dc
    )

/*++

Routine Description:

    This routine repaints the matrix UI.

Arguments:

    Dc - Supplies the device context to paint on.

Return Value:

    None.

--*/

{

    UCHAR BlueValue;
    LOGBRUSH BrushStyle;
    HPEN ColoredPen;
    UCHAR GreenValue;
    HPEN OffPen;
    HPEN OriginalPen;
    DWORD PenStyle;
    USHORT Pixel;
    UCHAR RedValue;
    UCHAR WhiteValue;
    ULONG XPixel;
    ULONG XPosition;
    ULONG YPixel;
    ULONG YPosition;

    ColoredPen = NULL;

    //
    // Set up the pen for off pixels.
    //

    PenStyle = PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND;
    BrushStyle.lbStyle = BS_SOLID;
    BrushStyle.lbColor = OFF_COLOR;
    BrushStyle.lbHatch = 0;
    OffPen = ExtCreatePen(PenStyle, MATRIX_PIXEL_WIDTH, &BrushStyle, 0, NULL);
    if (OffPen == NULL) {
        goto RefreshMatrixEnd;
    }

    OriginalPen = SelectObject(Dc, OffPen);
    for (YPixel = 0; YPixel < MATRIX_HEIGHT; YPixel += 1) {
        for (XPixel = 0; XPixel < MATRIX_WIDTH; XPixel += 1) {

            //
            // If there's a text color, use that unless the screen color has
            // been updated, in which case obliterate the text color.
            //

            if (HlTextColor[YPixel][XPixel] != 0) {
                Pixel = HlTextColor[YPixel][XPixel];
                if (KeMatrix[YPixel][XPixel] != HlMatrix[YPixel][XPixel]) {
                    Pixel = KeMatrix[YPixel][XPixel];
                    HlMatrix[YPixel][XPixel] = Pixel;
                    HlTextColor[YPixel][XPixel] = 0;
                }

            } else {
                Pixel = KeMatrix[YPixel][XPixel];
                HlMatrix[YPixel][XPixel] = Pixel;
            }

            if (Pixel != 0) {
                BrushStyle.lbColor = HlpPixelToColorRef(Pixel);
                ColoredPen = ExtCreatePen(PenStyle,
                                          MATRIX_PIXEL_WIDTH,
                                          &BrushStyle,
                                          0,
                                          NULL);

                if (ColoredPen == NULL) {
                    continue;
                }

                SelectObject(Dc, ColoredPen);
            }

            XPosition = MATRIX_PIXEL_SPACING + (XPixel * MATRIX_PIXEL_SPACING);
            YPosition = MATRIX_PIXEL_SPACING + (YPixel * MATRIX_PIXEL_SPACING);
            MoveToEx(Dc, XPosition, YPosition, NULL);
            LineTo(Dc, XPosition, YPosition);
            if (Pixel != 0) {
                SelectObject(Dc, OffPen);
                DeleteObject(ColoredPen);
            }
        }
    }

    //
    // Redraw trackball 1.
    //

    Pixel = KeTrackball1;
    RedValue = PIXEL_RED(Pixel);
    GreenValue = PIXEL_GREEN(Pixel);
    BlueValue = PIXEL_BLUE(Pixel);
    WhiteValue = WHITEPIXEL_TRACKBALL1(KeWhiteLeds);
    if (WhiteValue > RedValue) {
        RedValue = WhiteValue;
    }

    if (WhiteValue * 2 > GreenValue) {
        GreenValue = WhiteValue * 2;
    }

    if (WhiteValue > BlueValue) {
        BlueValue = WhiteValue;
    }

    BrushStyle.lbColor =
                HlpPixelToColorRef(RGB_PIXEL(RedValue, GreenValue, BlueValue));

    ColoredPen = ExtCreatePen(PenStyle,
                              TRACKBALL_WIDTH,
                              &BrushStyle,
                              0,
                              NULL);

    if (ColoredPen != NULL) {
        SelectObject(Dc, ColoredPen);
        MoveToEx(Dc, TRACKBALL1_X, TRACKBALL_Y, NULL);
        LineTo(Dc, TRACKBALL1_X, TRACKBALL_Y);
        SelectObject(Dc, OffPen);
        DeleteObject(ColoredPen);
    }

    HlTrackball1 = Pixel;

    //
    // Redraw trackball 2.
    //

    Pixel = KeTrackball2;
    RedValue = PIXEL_RED(Pixel);
    GreenValue = PIXEL_GREEN(Pixel);
    BlueValue = PIXEL_BLUE(Pixel);
    WhiteValue = WHITEPIXEL_TRACKBALL2(KeWhiteLeds);
    if (WhiteValue > RedValue) {
        RedValue = WhiteValue;
    }

    if (WhiteValue > GreenValue) {
        GreenValue = WhiteValue;
    }

    if (WhiteValue > BlueValue) {
        BlueValue = WhiteValue;
    }

    BrushStyle.lbColor =
                HlpPixelToColorRef(RGB_PIXEL(RedValue, GreenValue, BlueValue));

    ColoredPen = ExtCreatePen(PenStyle,
                              TRACKBALL_WIDTH,
                              &BrushStyle,
                              0,
                              NULL);

    if (ColoredPen != NULL) {
        SelectObject(Dc, ColoredPen);
        MoveToEx(Dc, TRACKBALL2_X, TRACKBALL_Y, NULL);
        LineTo(Dc, TRACKBALL2_X, TRACKBALL_Y);
        SelectObject(Dc, OffPen);
        DeleteObject(ColoredPen);
    }

    HlTrackball2 = Pixel;

    //
    // Redraw the standby LED.
    //

    Pixel = WHITEPIXEL_STANDBY(KeWhiteLeds);
    RedValue = Pixel;
    GreenValue = Pixel;
    BlueValue = Pixel;
    BrushStyle.lbColor =
                HlpPixelToColorRef(RGB_PIXEL(RedValue, GreenValue, BlueValue));

    ColoredPen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID,
                              STANDBY_LED_WIDTH,
                              &BrushStyle,
                              0,
                              NULL);

    if (ColoredPen != NULL) {
        SelectObject(Dc, ColoredPen);
        MoveToEx(Dc, STANDBY_X, TRACKBALL_Y, NULL);
        LineTo(Dc, STANDBY_X, TRACKBALL_Y);
        SelectObject(Dc, OffPen);
        DeleteObject(ColoredPen);
    }

    HlWhiteLeds = KeWhiteLeds;

    //
    // Restore the original pen and clean up.
    //

    SelectObject(Dc, OriginalPen);
    DeleteObject(OffPen);

    //
    // Draw the LCD.
    //

    HlpRedrawLcd(Dc);

RefreshMatrixEnd:
    return;
}

VOID
HlpRedrawLcd (
    HDC Dc
    )

/*++

Routine Description:

    This routine repaints the matrix UI.

Arguments:

    Dc - Supplies the device context to paint on.

Return Value:

    None.

--*/

{

    CHAR LcdText[36];
    HFONT OriginalFont;
    RECT Rectangle;
    BOOLEAN Result;

    Rectangle.left = LCD_X;
    Rectangle.top = LCD_Y;
    Rectangle.bottom = Rectangle.top + LCD_HEIGHT;
    Rectangle.right = Rectangle.left + LCD_WIDTH;

    //
    // Write out the characters.
    //

    sprintf(LcdText, "%-16s\r\n%-16s", HlLcdLine1, HlLcdLine2);
    OriginalFont = SelectObject(Dc, HlLcdFont);
    SetTextColor(Dc, LCD_FOREGROUND);
    SetBkColor(Dc, LCD_BACKGROUND);
    Result = DrawText(Dc, LcdText, strlen(LcdText), &Rectangle, 0);
    if (Result == 0) {
        MessageBox(NULL, "TextOut failed on the LCD.", "Error", MB_OK);
        PostQuitMessage(0);
    }

    SelectObject(Dc, OriginalFont);

    return;
}

COLORREF
HlpPixelToColorRef (
    USHORT Pixel
    )

/*++

Routine Description:

    This routine converts a pixel to a COLORREF format that Win32 pens can
    understand.

Arguments:

    Pixel - Supplies the pixel color.

Return Value:

    None.

--*/

{

    USHORT BlueValue;
    USHORT GreenValue;
    USHORT RedValue;

    RedValue = INTENSITY_OFF +
               ((PIXEL_RED(Pixel) * (0xFF - INTENSITY_OFF)) / 31);

    GreenValue = INTENSITY_OFF +
                 ((PIXEL_GREEN(Pixel) * (0xFF - INTENSITY_OFF)) / 31);

    BlueValue = INTENSITY_OFF +
                ((PIXEL_BLUE(Pixel) * (0xFF - INTENSITY_OFF)) / 31);

    return RGB(RedValue, GreenValue, BlueValue);
}

BOOLEAN
HlpInitializeLcd (
    HWND Window
    )

/*++

Routine Description:

    This routine initialize the LCD UI element.

Arguments:

    Window - Supplies the window handle of the parent window.

Return Value:

    None.

--*/

{

    BOOLEAN Result;

    //
    // Create and cache the font object.
    //

    if (HlLcdFont == NULL) {
        HlLcdFont = CreateFont(0,
                               0,
                               0,
                               0,
                               FW_NORMAL,
                               FALSE,
                               FALSE,
                               FALSE,
                               DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS,
                               DEFAULT_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE,
                               LCD_FONT_NAME);

        if (HlLcdFont == NULL) {
            Result = FALSE;
            goto InitializeLcdEnd;
        }
    }

    Result = TRUE;

InitializeLcdEnd:
    if (Result == FALSE) {
        if (HlLcdFont != NULL) {
            DeleteObject(HlLcdFont);
            HlLcdFont = NULL;
        }
    }

    return Result;
}

BOOLEAN
HlpProcessInputs (
    ULONG InputKey,
    BOOLEAN KeyDown
    )

/*++

Routine Description:

    This routine processes a new input key (both down and up).

Arguments:

    InputKey - Supplies the WPARAM containing the key that went down or up.

    KeyDown - Supplies a boolean indicating whether the key was pressed down
        (TRUE) or let up (FALSE).

Return Value:

    TRUE if the key was handled.

    FALSE if the key was not handled.

--*/

{

    BOOLEAN Handled;
    USHORT NewInputs;

    Handled = TRUE;
    NewInputs = 0;
    switch (InputKey) {
    case VK_RETURN:
        NewInputs |= INPUT_BUTTON2;
        break;

    case VK_SPACE:
        NewInputs |= INPUT_BUTTON1;
        break;

    case VK_LEFT:
        NewInputs |= INPUT_LEFT2;
        break;

    case VK_RIGHT:
        NewInputs |= INPUT_RIGHT2;
        break;

    case VK_UP:
        NewInputs |= INPUT_UP2;
        break;

    case VK_DOWN:
        NewInputs |= INPUT_DOWN2;
        break;

    case 'I':
        NewInputs |= INPUT_UP1;
        break;

    case 'K':
        NewInputs |= INPUT_DOWN1;
        break;

    case 'J':
        NewInputs |= INPUT_LEFT1;
        break;

    case 'L':
        NewInputs |= INPUT_RIGHT1;
        break;

    case 'M':
        NewInputs |= INPUT_MENU;
        break;

    case 'O':
        NewInputs |= INPUT_STANDBY;
        break;

    default:
        Handled = FALSE;
        break;
    }

    if (KeyDown != FALSE) {
        HlRawInputs |= NewInputs;
        HlInputEdges |= NewInputs;

    } else {
        HlRawInputs &= ~NewInputs;
    }

    return Handled;
}
