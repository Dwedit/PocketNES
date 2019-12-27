VERSION 5.00
Begin VB.Form frm 
   Caption         =   "NESPack 7"
   ClientHeight    =   4335
   ClientLeft      =   60
   ClientTop       =   450
   ClientWidth     =   5775
   LinkTopic       =   "Form1"
   OLEDropMode     =   1  'Manual
   ScaleHeight     =   4335
   ScaleWidth      =   5775
   StartUpPosition =   3  'Windows Default
   Begin VB.TextBox txtLog 
      Height          =   3495
      Left            =   120
      MultiLine       =   -1  'True
      OLEDropMode     =   1  'Manual
      ScrollBars      =   2  'Vertical
      TabIndex        =   1
      Top             =   720
      Width           =   5535
   End
   Begin VB.CommandButton cmdRun 
      Caption         =   "&Compress Files..."
      Default         =   -1  'True
      Height          =   495
      Left            =   120
      TabIndex        =   0
      Top             =   120
      Width           =   1455
   End
End
Attribute VB_Name = "frm"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = False
Attribute VB_PredeclaredId = True
Attribute VB_Exposed = False
Option Explicit

Private Declare Function ExpandEnvironmentStrings Lib "kernel32" Alias "ExpandEnvironmentStringsA" (ByVal lpSrc As String, ByVal lpDst As String, ByVal nSize As Long) As Long
Private Declare Function GetSystemDirectory Lib "kernel32" Alias "GetSystemDirectoryA" (ByVal lpBuffer As String, ByVal nSize As Long) As Long
Private Declare Function GetWindowsDirectory Lib "kernel32" Alias "GetWindowsDirectoryA" (ByVal lpBuffer As String, ByVal nSize As Long) As Long

Private Declare Function OpenProcess Lib "kernel32" _
(ByVal dwDesiredAccess As Long, ByVal bInheritHandle As Long, _
ByVal dwProcessId As Long) As Long

Private Declare Function GetExitCodeProcess Lib "kernel32" _
(ByVal hProcess As Long, lpExitCode As Long) As Long

Private Const STATUS_PENDING = &H103&
Private Const PROCESS_QUERY_INFORMATION = &H400

Private Type OPENFILENAME
    lStructSize As Long
    hwndOwner As Long
    hInstance As Long
    lpstrFilter As String
    lpstrCustomFilter As String
    nMaxCustFilter As Long
    nFilterIndex As Long
    lpstrFile As String
    nMaxFile As Long
    lpstrFileTitle As String
    nMaxFileTitle As Long
    lpstrInitialDir As String
    lpstrTitle As String
    flags As Long
    nFileOffset As Integer
    nFileExtension As Integer
    lpstrDefExt As String
    lCustData As Long
    lpfnHook As Long
    lpTemplateName As String
End Type

Private Declare Function GetOpenFileName Lib "comdlg32.dll" Alias _
    "GetOpenFileNameA" (pOpenFileName As OPENFILENAME) As Long

Private Const OFN_ALLOWMULTISELECT = &H200&
Private Const OFN_EXPLORER = &H80000
Private Const OFN_FILEMUSTEXIST = &H1000&
Private Const OFN_HIDEREADONLY = &H4&
Private Const OFN_PATHMUSTEXIST = &H800&

Sub ShowFileOpenDialog(ByRef FileList As Collection)
    Dim OpenFile As OPENFILENAME
    Dim lReturn As Long
    Dim FileDir As String
    Dim FilePos As Long
    Dim PrevFilePos As Long

    With OpenFile
        .lStructSize = Len(OpenFile)
        .hwndOwner = 0
        .hInstance = 0
        .lpstrFilter = "NES ROMS" + Chr(0) + "*.nes" + _
            Chr(0) + "All Files (*.*)" + Chr(0) + "*.*" + Chr(0) + Chr(0)
        .nFilterIndex = 1
        .lpstrFile = String(4096, 0)
        .nMaxFile = Len(.lpstrFile) - 1
        .lpstrFileTitle = .lpstrFile
        .nMaxFileTitle = .nMaxFile
        .lpstrInitialDir = "C:\"
        .lpstrTitle = "Load NES roms..."
        .flags = OFN_HIDEREADONLY + _
            OFN_PATHMUSTEXIST + _
            OFN_FILEMUSTEXIST + _
            OFN_ALLOWMULTISELECT + _
            OFN_EXPLORER
        lReturn = GetOpenFileName(OpenFile)
        If lReturn <> 0 Then
            FilePos = InStr(1, .lpstrFile, Chr(0))
            If Mid(.lpstrFile, FilePos + 1, 1) = Chr(0) Then
                FileList.Add .lpstrFile
            Else
                FileDir = Mid(.lpstrFile, 1, FilePos - 1)
                Do While True
                    PrevFilePos = FilePos
                    FilePos = InStr(PrevFilePos + 1, .lpstrFile, Chr(0))
                    If FilePos - PrevFilePos > 1 Then
                        FileList.Add FileDir + "\" + _
                            Mid(.lpstrFile, PrevFilePos + 1, _
                                FilePos - PrevFilePos - 1)
                    Else
                        Exit Do
                    End If
                Loop
            End If
        End If
    End With
End Sub

Sub SelectFiles()
    Dim FileList As Collection
    Set FileList = New Collection
    
    Dim I As Long
    Dim S As String

    Dim msg As VbMsgBoxResult
    Dim error As Integer
    ChDrive App.Path
    ChDir App.Path

    ShowFileOpenDialog FileList
    With FileList
        If .Count > 0 Then
            PackFiles FileList
        End If
    End With
End Sub

Sub PackFiles(ByRef Files As Collection)
    Dim I As Long
    Dim inFile As String, outFile As String, basename As String
    Dim NewSize As Long
    With Files
        If .Count > 0 Then
            For I = 1 To .Count
                inFile = .Item(I)
                Dim DotPos As Long
                DotPos = InStrRev2(inFile, ".")
                If DotPos > 0 Then
                    basename = Left(inFile, DotPos - 1)
                Else
                    basename = inFile
                End If
                outFile = basename + "_k.nes"
                NewSize = PackFile(inFile, outFile)
                If NewSize Then
                    TextOut "Compressed NES file " + outFile + " down to" + str(NewSize) + " bytes."
                Else
                    TextOut "Unable to pack " + inFile
                End If
            Next
        End If
    End With

End Sub


Sub GetChecksum(ByRef Prg() As Byte, ByRef CheckSum() As Byte)
    Dim Sum As Currency
    Dim Val As Currency
    
    Dim Bits8 As Currency
    Bits8 = 256
    Dim Bits16 As Currency
    Bits16 = Bits8 * Bits8
    Dim Bits24 As Currency
    Bits24 = Bits8 * Bits16
    Dim Bits32 As Currency
    Bits32 = Bits8 * Bits24
    
    Dim Highbit As Currency
    Highbit = Bits32
    Dim WordMSB As Currency
    WordMSB = Highbit / CCur(2&)
    
    
    Dim I As Long
    Dim P As Long
    P = 0
    For I = 0& To 127&
        'read unsigned 32 bit value
        Val = CCur(Prg(P + 0&)) + CCur(Prg(P + 1&)) * Bits8 + CCur(Prg(P + 2&)) * Bits16 + CCur(Prg(P + 3)) * Bits24
'       Val = CCur(Prg(P + 0&)) + ccur(Prg(P + 1&) * 256&) + ccur(Prg(P + 2&) * 65536)) + CCur(Prg(P + 3)) * CCur(16777216)
        Sum = Sum + Val
        If (Sum >= Highbit) Then Sum = Sum - Highbit
        P = P + 128
    Next
    
    ReDim CheckSum(3)
    Dim SumLong As Long
    Dim MSBState As Long
    MSBState = 0
    If Sum >= WordMSB Then
        MSBState = 1
        SumLong = Sum - WordMSB
    Else
        SumLong = Sum
    End If
    CheckSum(0) = SumLong Mod Bits8
    SumLong = SumLong \ Bits8
    CheckSum(1) = SumLong Mod Bits8
    SumLong = SumLong \ Bits8
    CheckSum(2) = SumLong Mod Bits8
    SumLong = SumLong \ Bits8
    CheckSum(3) = (SumLong Mod Bits8) + MSBState * 128
End Sub

Sub TextOut(msg As String)
    txtLog.Text = txtLog.Text + msg + vbCrLf
    txtLog.SelStart = Len(txtLog.Text)
    txtLog.SelLength = 0
End Sub

Function InStrRev2(str As String, lookfor As String) As Long
    Dim I As Long
    For I = Len(str) To 1 Step -1
        If Mid(str, I, 1) = lookfor Then Exit For
    Next
    InStrRev2 = I
End Function

Function sTempDirectory() As String
    Dim sOut As String
    sOut = Space(260)
    ExpandEnvironmentStrings "%TEMP%", sOut, 260
    sOut = Left(sOut, InStr(sOut, Chr(0)) - 1)
    sTempDirectory = sOut
End Function

Function GetArrSize(ByRef arr() As Byte) As Long
    On Error Resume Next
    GetArrSize = UBound(arr) + 1
    On Error GoTo 0
End Function

Sub AddArray(ByRef arr() As Byte, ByRef arr2() As Byte)
    Dim I As Long
    Dim ASize As Long, ASize2 As Long
    
    
    ASize = GetArrSize(arr)
    ASize2 = GetArrSize(arr2)
    ReDim Preserve arr(ASize + ASize2 - 1&)
    For I = ASize To ASize + ASize2 - 1&
        arr(I) = arr2(I - ASize)
    Next
End Sub

Function PackFile(inFile As String, outFile As String) As Long
    Dim FileNum As Long
    Dim FileSize As Long
    Dim NewFileSize As Long
    Dim FilePos As Long
    Dim LastByte As Byte
    Dim GetByte As Byte
    Dim Data() As Byte
    
    Dim RomBanks As Long, VRomBanks As Long
    Dim PRGSize As Long
    Dim CHRSize As Long
    Dim TotalSize As Long
    Dim Mapper As Long
    Dim DoSplit As Boolean
    
    Dim Data1() As Byte
    Dim Data2() As Byte
    
    Dim Trimpos As Long
    Dim Head() As Byte
    Dim TempDir As String, TempFile As String, TempFile2 As String, TempFile3 As String, TempFile4 As String
    
    TempDir = sTempDirectory()
    TempFile = TempDir + "\__nes_file__.prg"
    TempFile2 = TempDir + "\__nes_file__.chr"
    TempFile3 = TempDir + "\__nes_file_prg__.ap"
    TempFile4 = TempDir + "\__nes_file_chr__.ap"
    
    FileNum = FreeFile
    Open inFile For Binary As FileNum
        FileSize = LOF(FileNum) - 16
        If FileSize <= 15 Then
            Exit Function
            Close FileNum
        End If
        ReDim Head(16 - 1)
        Get FileNum, , Head
        RomBanks = Head(4)
        VRomBanks = Head(5)
        PRGSize = RomBanks * 16384&
        CHRSize = VRomBanks * 8192&
        TotalSize = PRGSize + CHRSize
        
        'size validation
        If TotalSize > FileSize Then
            TextOut "The NES header says the rom is " + Trim(str(TotalSize \ 1024)) + "k in size, but the actual file size is " + Trim(str(FileSize \ 1024)) + "k!"
            PackFile = 0
            Exit Function
        End If
        
        'too big validation
        If TotalSize > 256& * 1024& Then
            TextOut "File is too big!  Maximum supported size is 256k"
            PackFile = 0
            Exit Function
        End If
        
        DoSplit = False
        If TotalSize > 192& * 1024& Then
            DoSplit = True
        End If
        
        Head(9) = 0
        Head(10) = 0
        Head(11) = 0
        Head(12) = Asc("A")
        Head(13) = Asc("P")
        Head(14) = Asc("3")
        Head(15) = Asc("3")
        ReDim Preserve Head(19)
        
        Dim CheckSum() As Byte
        
'       Dim Data2() As Byte
        Dim I As Long
        
        If DoSplit Then
            ReDim Data1(128& * 1024& - 1)
            ReDim Data2(TotalSize - 128& * 1024& - 1)
            Get FileNum, , Data1
            Get FileNum, , Data2
        Else
            ReDim Data1(TotalSize - 1)
            Get FileNum, , Data1
        End If
        
        GetChecksum Data1, CheckSum
        Head(16) = CheckSum(0)
        Head(17) = CheckSum(1)
        Head(18) = CheckSum(2)
        Head(19) = CheckSum(3)
        
    '   Mapper = Head(6) \ 16 + (Head(7) And &HF0)
    '   If Head(8) Then Mapper = Mapper And 15
    '   Select Case Mapper
    '       Case 0, 1, 2, 3, 4, 7, 9, 11, 66, 99, 71, 180
    '       Case Else
    '           TextOut "Warning: Mapper " + Trim(str(Mapper)) + " is unsupported!"
    '   End Select
    Close FileNum
    
    Open TempFile For Output As FileNum
    Close FileNum
    Open TempFile For Binary As FileNum
        Put #FileNum, , Data1
    Close FileNum
    
    If DoSplit Then
        Open TempFile2 For Output As FileNum
        Close FileNum
        Open TempFile2 For Binary As FileNum
            Put #FileNum, , Data2
        Close FileNum
    End If
    
    ShellandWait ("apack " + Chr$(34) + TempFile + Chr$(34) + " " + Chr$(34) + TempFile3 + Chr$(34))
    If DoSplit Then
        ShellandWait ("apack " + Chr$(34) + TempFile2 + Chr$(34) + " " + Chr$(34) + TempFile4 + Chr$(34))
        Open TempFile4 For Binary As FileNum
            FileSize = LOF(FileNum)
            If FileSize = 0 Then
                Close #FileNum
                Kill TempFile
                Kill TempFile2
                Kill TempFile3
                Kill TempFile4
                PackFile = 0
                TextOut "Failed to create an output file in the temp directory!"
                Exit Function
            End If
            
            ReDim Data2(FileSize - 1)
            Get #FileNum, , Data2
        Close FileNum
        Head(9) = FileSize And 255&
        Head(10) = (FileSize \ 256&) And 255&
        Head(11) = (FileSize \ 65536) And 255&
        AddArray Data, Data2
    End If
    
    Open TempFile3 For Binary As FileNum
        FileSize = LOF(FileNum)
        If FileSize = 0 Then
            Close #FileNum
            Kill TempFile
            Kill TempFile3
            PackFile = 0
            TextOut "Failed to create an output file in the temp directory!"
            Exit Function
        End If
        ReDim Data1(FileSize - 1)
        Get #FileNum, , Data1
        AddArray Data, Data1
    Close #FileNum
    
    Kill TempFile
    Kill TempFile3
    If DoSplit Then
        Kill TempFile2
        Kill TempFile4
    End If
    
    Dim OutSize As Long
    Dim Padding As Long
    OutSize = UBound(Data) + 1&
    OutSize = ((OutSize - 1&) Or 3&) + 1&
    ReDim Preserve Data(OutSize - 1&)
    
    Open outFile For Output As FileNum
    Close #FileNum
    Open outFile For Binary As FileNum
        Put #FileNum, , Head
        Put #FileNum, , Data
    Close #FileNum
    
    PackFile = UBound(Data) + UBound(Head) - 1& - 1&
    
End Function


Private Function ShellandWait(ExeFullPath As String, _
Optional TimeOutValue As Long = 0) As Boolean
    
    Dim lInst As Long
    Dim lStart As Long
    Dim lTimeToQuit As Long
    Dim sExeName As String
    Dim lProcessId As Long
    Dim lExitCode As Long
    Dim bPastMidnight As Boolean
    
    On Error GoTo ErrorHandler

    lStart = CLng(Timer)
    sExeName = ExeFullPath

    'Deal with timeout being reset at Midnight
    If TimeOutValue > 0 Then
        If lStart + TimeOutValue < 86400 Then
            lTimeToQuit = lStart + TimeOutValue
        Else
            lTimeToQuit = (lStart - 86400) + TimeOutValue
            bPastMidnight = True
        End If
    End If

    lInst = Shell(sExeName, vbMinimizedNoFocus)
    
lProcessId = OpenProcess(PROCESS_QUERY_INFORMATION, False, lInst)

    Do
        Call GetExitCodeProcess(lProcessId, lExitCode)
        DoEvents
        If TimeOutValue And Timer > lTimeToQuit Then
            If bPastMidnight Then
                 If Timer < lStart Then Exit Do
            Else
                 Exit Do
            End If
    End If
    Loop While lExitCode = STATUS_PENDING
    
    ShellandWait = True
   
ErrorHandler:
ShellandWait = False
Exit Function
End Function


Private Sub cmdRun_Click()
    SelectFiles
End Sub

Sub Drag(Data As DataObject, Effect As Long)
    Dim FileList As Collection
    Set FileList = New Collection
    
    Dim FileName As Variant
    For Each FileName In Data.Files
        FileList.Add (FileName)
    Next
    
    PackFiles FileList
End Sub

Private Sub Form_OLEDragDrop(Data As DataObject, Effect As Long, Button As Integer, Shift As Integer, X As Single, Y As Single)
    Drag Data, Effect
End Sub

Private Sub txtLog_OLEDragDrop(Data As DataObject, Effect As Long, Button As Integer, Shift As Integer, X As Single, Y As Single)
    Drag Data, Effect
End Sub

Private Sub cmdRun_OLEDragDrop(Data As DataObject, Effect As Long, Button As Integer, Shift As Integer, X As Single, Y As Single)
    Drag Data, Effect
End Sub

