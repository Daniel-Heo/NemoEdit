//﻿*******************************************************************************
//    파     일     명 : NemoEdit.cpp
//    프로그램명칭 : 네모 에디터 컨트롤
//    프로그램용도 : 윈도우즈 MFC 기반의 텍스트 에디터 컨트롤 ( 빠르고, 대용량 가능 )
//    참  고  사  항  : 한글이 지원되면서 마음에 드는 오픈소스 에디터를 구하기 어려워서 직접 만들었습니다.
//
//    작    성    자 : Daniel Heo ( https://github.com/Daniel-Heo/NemoEdit )
//    라 이  센 스  : Dual License
//                            If you are not a citizen of the Republic of Korea : AGPL 3.0 License
//                            If you are a citizen of the Republic of Korea : MIT License
//    ----------------------------------------------------------------------------
//    수정일자    수정자      수정내용
//    =========== =========== ====================================================
//    2025.3.19   Daniel Heo  최초 생성
//*******************************************************************************
#include "pch.h"
#include "NemoEdit.h"
#include <afxpriv.h>   // AfxRegisterWndClass 사용을 위해

#pragma comment(lib, "imm32.lib") // IMM32 라이브러리 링크
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

// NemoEdit 클래스 생성자 - 기본 초기화
NemoEdit::NemoEdit()
    : m_isReadOnly(false), m_showLineNumbers(true),
      m_wordWrap(true), m_wordWrapWidth(0),
      m_lineSpacing(5),
	  m_margin({ 5, 15, 5, 0 }),
      m_lineHeight(0), m_charWidth(0),
	  m_scrollX(0), m_scrollYLine(0), m_scrollYWrapLine(0),
      m_nextDiffNum(0),
	  m_isUseScrollCtrl(FALSE), m_showScrollBars(FALSE),
	  m_tabSize(4), m_maxWidth(0), m_numberAreaWidth(0),
      m_lastClickTime(0), m_clickCount(0)
    , m_imeWidth(0)
 {
    // 텍스트 라인 관련
    m_rope.insert(0, L"");
    // 캐럿 관련
    m_caretPos = TextPos(0, 0);
    m_caretVisible = false;
    m_selectInfo.start = m_selectInfo.end = m_selectInfo.anchor = m_caretPos;
    m_selectInfo.isSelecting = false;
	m_selectInfo.isSelected = false;

	// 색상 관련
    m_colorInfo.text = RGB(250, 250, 250);
    m_colorInfo.textBg = RGB(28, 29, 22);
    m_colorInfo.lineNum = RGB(140, 140, 140);
    m_colorInfo.lineNumBg = RGB(28, 29, 22);
    m_colorInfo.select = RGB(0, 102, 204);
	
	// undo, redo 스택 초기화
    m_undoStack.reserve(100);
    m_redoStack.reserve(100);

    // IME
	m_imeComposition.isComposing = false;

	// 단어 경계 검사
	InitializeWordDelimiters();

#ifdef _DEBUG
    // 디버그 모드에서만 실행되는 코드
    _CrtSetDbgFlag(0); // 메모리 누수 보고 비활성화 : 대용량 시에 기다리는 시간이 아까워서.
#endif
}

// 소멸자
NemoEdit::~NemoEdit() {
    // D2Render 정리
    m_d2Render.Shutdown();
}

// 윈도우 클래스 등록 및 컨트롤 생성
BOOL NemoEdit::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID) {
    LPCTSTR className = AfxRegisterWndClass(CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW,
        ::LoadCursor(NULL, IDC_IBEAM),
        NULL, NULL);
    // 기본 스타일 설정 (자식 윈도우, 스크롤바 포함) - WS_CLIPCHILDREN 추가하여 자식 윈도우 영역 그리기 방지
    dwStyle |= WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPCHILDREN;
    dwStyle &= ~(WS_BORDER | WS_DLGFRAME); // 테두리 제거
    BOOL res = CreateEx(WS_EX_TRANSPARENT, className, _T(""), dwStyle,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        pParentWnd->GetSafeHwnd(), (HMENU)(UINT_PTR)nID);

    // CreateEx 실행 후 Z-order 설정 추가
    if (res) {
        // Z-order를 맨 아래로 설정
        ::SetWindowPos(m_hWnd, HWND_BOTTOM, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (!m_d2Render.Initialize(GetSafeHwnd())) {
            AfxMessageBox(L"Direct2D 초기화에 실패했습니다.");
            return -1;
        }

        // 기본 폰트 설정 (Consolas는 거의 모든 Windows 시스템에 기본 설치됨)
        //m_d2Render.SetFont(L"D2Coding", 16, false, false);
        m_d2Render.SetFont(L"Consolas", 16, false, false);
        

        //// 색상 설정
        m_d2Render.SetTextColor(m_colorInfo.text);
        m_d2Render.SetBgColor(m_colorInfo.textBg);
        m_d2Render.SetLineNumColor(m_colorInfo.lineNum);
        m_d2Render.SetLineNumBgColor(m_colorInfo.lineNumBg);
        m_d2Render.SetSelectionColors(m_colorInfo.text, m_colorInfo.select);

        m_lineHeight = m_d2Render.GetLineHeight();
        m_charWidth = m_charWidth = GetTextWidth(L"080") - GetTextWidth(L"08");

        CRect client;
        GetClientRect(&client);
        m_d2Render.Resize(client.Width(), client.Height());
    }

    HideIME();

    // 키보드 입력 속도를 제일 빠르게
    SystemParametersInfo(SPI_SETKEYBOARDDELAY, 0, NULL, 0); // 지연 속도 : 0~3 (0이 가장 빠름)
    SystemParametersInfo(SPI_SETKEYBOARDSPEED, 31, NULL, 0); // 반복 속도 : 0~31 (31이 가장 빠름)

    // 워드랩 설정
    SetWordWrap(m_wordWrap);

    // 캐럿 초기화
    EnsureCaretVisible();

    // 스크롤바 초기 설정 적용
    SetScrollCtrl(m_showScrollBars);

    return res;
}

BEGIN_MESSAGE_MAP(NemoEdit, CWnd)
    ON_WM_CREATE()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_VSCROLL()
    ON_WM_HSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_SETFOCUS()
    ON_WM_KILLFOCUS()
    ON_WM_CHAR()
    ON_WM_KEYDOWN()
    // IME  
    ON_MESSAGE(WM_IME_STARTCOMPOSITION, OnImeStartComposition)
    ON_MESSAGE(WM_IME_COMPOSITION, OnImeComposition)
    ON_MESSAGE(WM_IME_CHAR, OnImeChar)
    ON_MESSAGE(WM_IME_ENDCOMPOSITION, OnImeEndComposition)
    ON_WM_LBUTTONDBLCLK()
END_MESSAGE_MAP()


int NemoEdit::GetLineWidth(int lineIndex) {
    std::wstring line = m_rope.getLine(lineIndex);
    line = ExpandTabs(line);
    return GetTextWidth(line);
}

// 현재 화면에 표시되는 라인 중 가장 긴 라인의 너비를 계산
int NemoEdit::GetMaxWidth() {
    if (m_rope.getSize() == 0) {
        return 0;
    }

    return m_maxWidth;
}

// 라인 번호 영역 너비 계산
int NemoEdit::CalculateNumberAreaWidth() {
    if (!m_showLineNumbers) {
        return 0;
    }

    int totalLines = static_cast<int>(m_rope.getSize());
    if (totalLines < m_nextDiffNum) {
        return m_numberAreaWidth;
    }
    else {
        if (m_nextDiffNum == 0) m_nextDiffNum = 1;
        m_nextDiffNum *= 10;
    }

    int digits = 1;
    while (totalLines >= 10) {
        digits++;
        totalLines /= 10;
    }
    m_numberAreaWidth = digits * m_charWidth + 20;
    return m_numberAreaWidth;
}

// OnCreate: 컨트롤 생성 시 초기화
int NemoEdit::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if(CWnd::OnCreate(lpCreateStruct) == -1)
        return -1;

    return 0;
}

// 폰트 변경 (LOGFONT 사용) : font 변경 메인 코어
void NemoEdit::ApplyFont() {
    m_lineHeight = m_d2Render.GetLineHeight() + m_lineSpacing;
    m_charWidth = GetTextWidth(L"080")-GetTextWidth(L"08"); // 공백 문자 너비로 대체
    m_imeWidth = GetTextWidth(L"한");
    m_nextDiffNum = 0; // numLineArea 재계산
    CreateSolidCaret(2, m_lineHeight - m_lineSpacing); // test
    RecalcScrollSizes();
    Invalidate(FALSE);
}

void NemoEdit::SetFontSize(int size) {
    m_d2Render.SetFontSize(size);
    ApplyFont();
}

int NemoEdit::GetFontSize() {
    return m_d2Render.GetFontSize();
}

void NemoEdit::SetFont(std::wstring fontName, int fontSize, bool bold, bool italic) {
    m_d2Render.SetFont(fontName, fontSize, bold, italic);
    ApplyFont();
}

void NemoEdit::GetFont(std::wstring& fontName, int& fontSize, bool& bold, bool& italic) {
	m_d2Render.GetFont(fontName, fontSize, bold, italic);
}

void NemoEdit::SetTabSize(int size) {
    m_tabSize = size;
    RecalcScrollSizes();
    Invalidate(FALSE);
}

int NemoEdit::GetTabSize() {
    return m_tabSize;
}

// 추가 줄 간격 설정
void NemoEdit::SetLineSpacing(int spacing) {
    m_lineSpacing = spacing;
    m_lineHeight = m_d2Render.GetLineHeight() + m_lineSpacing;
    RecalcScrollSizes();
    Invalidate(FALSE);
}

// 자동 줄바꿈 설정/해제
void NemoEdit::SetWordWrap(bool enable) {
    m_wordWrap = enable;
    if (m_wordWrap) {
        // 자동 줄바꿈 시 가로 스크롤 불필요 (숨김)
        m_scrollX = 0;
        NemoShowScrollBar(SB_HORZ, FALSE);

        CRect client;
        GetClientRect(&client);
        m_wordWrapWidth = client.Width() - CalculateNumberAreaWidth() - m_margin.right - m_margin.left;
    }
    else {
        NemoShowScrollBar(SB_HORZ, TRUE);
    }
    RecalcScrollSizes();
    Invalidate(FALSE);
}

// 라인 번호 표시 설정/해제
void NemoEdit::ShowLineNumbers(bool show) {
    m_showLineNumbers = show;
    EnsureCaretVisible();
    RecalcScrollSizes();
    Invalidate(FALSE);
}

// 읽기 전용 설정
void NemoEdit::SetReadOnly(bool isReadOnly) {
	m_isReadOnly = isReadOnly;

    // 읽기 전용 모드로 전환하면 캐럿 숨기기
    if (m_isReadOnly && m_caretVisible) {
        HideCaret();
        m_caretVisible = false;
    }
    // 읽기 전용 해제하면 캐럿 표시하기
    else if (!m_isReadOnly && ::GetFocus() == m_hWnd && !m_caretVisible) {
        ShowCaret();
        m_caretVisible = true;
    }
}

// 여백 설정
void NemoEdit::SetMargin(int left, int right, int top, int bottom) {
	m_margin.left = left;
	m_margin.right = right;
	m_margin.top = top;
	m_margin.bottom = bottom;
	Invalidate(FALSE);
}

// 텍스트 색상 설정
void NemoEdit::SetTextColor(COLORREF textColor, COLORREF bgColor) {
	m_colorInfo.text = textColor;
	m_colorInfo.textBg = bgColor;
    m_d2Render.SetTextColor(m_colorInfo.text);
    m_d2Render.SetBgColor(m_colorInfo.textBg);
	Invalidate(FALSE);
}

void NemoEdit::SetTextColor(COLORREF textColor) {
    m_colorInfo.text = textColor;
    m_d2Render.SetTextColor(m_colorInfo.text);
    Invalidate(FALSE);
}

void NemoEdit::SetBgColor(COLORREF textBgColor, COLORREF lineBgColor) {
    m_colorInfo.textBg = textBgColor;
    m_colorInfo.lineNumBg = lineBgColor;
    m_d2Render.SetBgColor(m_colorInfo.textBg);
    m_d2Render.SetLineNumBgColor(m_colorInfo.lineNumBg);
    Invalidate(FALSE);
}

void SetBgColior(COLORREF textColor);

COLORREF NemoEdit::GetTextColor() {
    return m_colorInfo.text;
}

COLORREF NemoEdit::GetTextBgColor() {
    return m_colorInfo.textBg;
}

// 라인 번호 색상 설정
void NemoEdit::SetLineNumColor(COLORREF lineNumColor, COLORREF bgColor) {
	m_colorInfo.lineNum = lineNumColor;
	m_colorInfo.lineNumBg = bgColor;
    m_d2Render.SetLineNumColor(m_colorInfo.lineNum);
    m_d2Render.SetLineNumBgColor(m_colorInfo.lineNumBg);
	Invalidate(FALSE);
}

// 에디터 전체 텍스트 설정
void NemoEdit::SetText(const std::wstring& text) {
    // 화면 갱신 일시 중지
    SetRedraw(FALSE);
    ClearText();

    // 개행 기준으로 문자열 파싱
    std::list<std::wstring> lines;
    SplitTextByNewlines((std::wstring & )text, lines);

    if (lines.empty()) {
        lines.push_back(L"");
    }

    // SPLIT_THRESHOLD 값을 넘는 경우 insertMultiple 사용
    if (lines.size() > SPLIT_THRESHOLD) {
        m_rope.insertMultiple(0, lines);
    }
    else {
        // 적은 양의 텍스트는 개별 삽입
        for (const auto& line : lines) {
            m_rope.insertBack(line);
        }
    }

    // 최대 라인 사이즈 초기화
    m_maxWidth = 0;

    // Undo/Redo 스택 초기화
    m_undoStack.clear();
    m_redoStack.clear();

    m_caretPos = TextPos(0, 0);
    EnsureCaretVisible();
    SetRedraw(TRUE);
    RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    HideIME();
}

// 에디터 전체 텍스트 얻기 ('\n'으로 구분)
std::wstring NemoEdit::GetText() {
	return m_rope.getText();
}

void NemoEdit::AddText(std::wstring text) {
    // 라인이 없는 경우 SetText 호출 (텍스트 초기화)
    if (m_rope.getSize() == 0) {
        SetText(text);
        return;
    }

    // 개행 기준으로 문자열 파싱
    std::list<std::wstring> lines;
    SplitTextByNewlines(text, lines);

    // 텍스트가 없으면 종료
    if (lines.empty()) {
        return;
    }

    // 추가 위치는 마지막 라인 다음 (마지막 라인의 인덱스 + 1)
    int insertIndex = (int)m_rope.getSize();

    // Undo 레코드 준비
    //UndoRecord rec;
    //rec.type = UndoRecord::Insert;
    //rec.start = TextPos(insertIndex, 0);
    //rec.text = text;  // 원본 텍스트 그대로 저장

    size_t endColumn = lines.back().length(); // lines가 insert 후에 사라지기 때문에 그전에 값을 얻어놓음.

    // SPLIT_THRESHOLD 값을 넘는 경우 insertMultiple 사용
    if (lines.size() > SPLIT_THRESHOLD) {
        m_rope.insertMultiple(insertIndex, lines);
    }
    else {
        // 적은 양의 텍스트는 개별 삽입
        for (const auto& line : lines) {
            m_rope.insert(insertIndex++, line);
        }
    }

    // 캐럿 위치 갱신 - 추가된 텍스트의 마지막 위치로
    insertIndex = (int)m_rope.getSize() - 1;
    m_caretPos.lineIndex = insertIndex;
    m_caretPos.column = (int)endColumn;

    // 선택 영역 초기화
    m_selectInfo.start = m_selectInfo.end = m_selectInfo.anchor = m_caretPos;
    m_selectInfo.isSelected = false;
    m_selectInfo.isSelecting = false;

    // Undo/Redo 스택 갱신
    //m_undoStack.push_back(rec);
    //m_redoStack.clear();

    // 화면 갱신 및 커서 위치 보정
    EnsureCaretVisible();
    Invalidate(FALSE);
}

void NemoEdit::ClearText() {
    m_rope.clear();
    m_nextDiffNum = 0; // numLineArea 재계산
    m_caretPos = TextPos(0, 0);
    m_scrollX = 0;
    m_scrollYLine = 0;
    m_scrollYWrapLine = 0;
    m_selectInfo.start = m_selectInfo.end = m_selectInfo.anchor = m_caretPos;
    m_selectInfo.isSelected = false;
    m_selectInfo.isSelecting = false;
	
    UpdateCaretPosition(); // 케럿 초기화 적용
}

// Tab 문자를 주어진 크기의 공백으로 변환하는 함수
std::wstring NemoEdit::ExpandTabs(const std::wstring& text) {
    // 결과를 저장할 문자열
    std::wstring result;
    result.reserve(text.length() * 2);
    // 탭용 공백 문자열을 미리 한 번만 생성
    const std::wstring tabSpaces(m_tabSize, L' ');

    // 입력 문자열을 순회하며 탭을 공백으로 변환
    for (size_t i = 0; i < text.length(); ++i) {
        if (text[i] == L'\t') {
            // 탭 문자 발견 시 지정된 수의 공백 추가
            result.append(tabSpaces);
        }
        else {
            // 일반 문자는 그대로 복사
            result.push_back(text[i]);
        }
    }

    return result;
}

// Tab 문자의 개수를 카운팅
int NemoEdit::TabCount(const std::wstring& text, int endPos) {
    int endCnt = min(endPos, text.length());
    int tabCnt = 0;
    // 입력 문자열을 순회하며 탭을 공백으로 변환
    for (size_t i = 0; i < endCnt; i++)
        if (text[i] == L'\t') tabCnt++;

    return tabCnt;
}

// 트리플 클릭 처리 메서드 구현
void NemoEdit::HandleTripleClick(CPoint point)
{
    // 현재 클릭 위치에 해당하는 텍스트 위치 가져오기
    TextPos pos = GetTextPosFromPoint(point);

    // 선택 영역 시작 위치를 라인의 시작으로 설정
    m_selectInfo.start.lineIndex = pos.lineIndex;
    m_selectInfo.start.column = 0;

    // 선택 영역 끝 위치를 라인의 끝으로 설정
    m_selectInfo.end.lineIndex = pos.lineIndex;
    m_selectInfo.end.column = (int)m_rope.getLineSize(pos.lineIndex);

    // 선택 상태 설정
    m_selectInfo.isSelected = true;
    m_selectInfo.anchor = m_selectInfo.start;

    // 캐럿 위치를 라인의 끝으로 설정
    m_caretPos = m_selectInfo.end;

    // 화면 갱신
    UpdateCaretPosition();
    Invalidate(FALSE);
}

// 구분자 배열 초기화
void NemoEdit::InitializeWordDelimiters() {
    // 배열 초기화 (모두 false로 설정)
    memset(isDivChar, 0, sizeof(isDivChar));

    // ASCII 범위 내의 문자들에 대한 구분자 설정
    // 공백 문자
    isDivChar[' '] = true;
    isDivChar['\t'] = true;
    isDivChar['\r'] = true;
    isDivChar['\n'] = true;

    // 구두점
    isDivChar['.'] = true;
    isDivChar[','] = true;
    isDivChar[';'] = true;
    isDivChar[':'] = true;
    isDivChar['!'] = true;
    isDivChar['?'] = true;
    isDivChar['\''] = true;
    isDivChar['"'] = true;
    isDivChar['`'] = true;

    // 괄호
    isDivChar['('] = true;
    isDivChar[')'] = true;
    isDivChar['['] = true;
    isDivChar[']'] = true;
    isDivChar['{'] = true;
    isDivChar['}'] = true;
    isDivChar['<'] = true;
    isDivChar['>'] = true;

    // 연산자 및 기타 특수문자
    isDivChar['+'] = true;
    isDivChar['-'] = true;
    isDivChar['*'] = true;
    isDivChar['/'] = true;
    isDivChar['='] = true;
    isDivChar['&'] = true;
    isDivChar['|'] = true;
    isDivChar['^'] = true;
    isDivChar['~'] = true;
    isDivChar['@'] = true;
    isDivChar['#'] = true;
    isDivChar['$'] = true;
    isDivChar['%'] = true;
    isDivChar['\\'] = true;

    // 주의: 언더스코어(_)는 구분자로 처리하지 않음 (프로그래밍 변수명에 포함)
}

// 단어 구분자인지 확인
bool NemoEdit::IsWordDelimiter(wchar_t ch) {
    // ASCII 범위 내 문자는 미리 계산된 배열 사용
    if (ch < 256) {
        return isDivChar[ch];
    }

    // ASCII 범위 밖의 문자는 공백이나 구두점인지만 확인
    return iswspace(ch) || iswpunct(ch) && ch != L'_';
}

// 특정 위치의 단어 경계 찾기 (단순화 버전)
void NemoEdit::FindWordBoundary(const std::wstring& text, int position, int& start, int& end) {
    int length = static_cast<int>(text.length());

    // 빈 문자열이거나 범위를 벗어난 경우
    if (length == 0 || position < 0 || position >= length) {
        start = end = position;
        return;
    }

    wchar_t currentChar = text[position];
    bool isDelimiter = IsWordDelimiter(currentChar);

    // 현재 위치가 단어 구분자인 경우
    if (isDelimiter) {
        // 같은 종류의 구분자 그룹으로 확장
        bool isSpace = iswspace(currentChar);

        // 시작 위치 찾기
        start = position;
        while (start > 0) {
            wchar_t prevChar = text[start - 1];

            // 공백 문자 그룹
            if (isSpace) {
                if (!iswspace(prevChar)) break;
            }
            // 같은 구분자 그룹 (예: === 또는 --- 등)
            else if (currentChar == prevChar) {
                // 계속 확장
            }
            // 다른 구분자면 중단
            else {
                break;
            }

            start--;
        }

        // 끝 위치 찾기
        end = position;
        while (end < length - 1) {
            wchar_t nextChar = text[end + 1];

            // 공백 문자 그룹
            if (isSpace) {
                if (!iswspace(nextChar)) break;
            }
            // 같은 구분자 그룹
            else if (currentChar == nextChar) {
                // 계속 확장
            }
            // 다른 구분자면 중단
            else {
                break;
            }

            end++;
        }
    }
    // 일반 단어의 경우
    else {
        // 단어 시작 위치 찾기
        start = position;
        while (start > 0 && !IsWordDelimiter(text[start - 1])) {
            start--;
        }

        // 단어 끝 위치 찾기
        end = position;
        while (end < length - 1 && !IsWordDelimiter(text[end + 1])) {
            end++;
        }
    }

    end++; // 끝 위치는 마지막 문자 다음 위치
}

/* -------------------------------------------------------
 * Undo 헬퍼 함수 구현
 * -----------------------------------------------------*/

// 현재 상태를 레코드에 저장
void NemoEdit::SaveCurrentState(UndoRecord& record) {
    record.caretPos = m_caretPos;
    record.hadSelection = m_selectInfo.isSelected;
    if (record.hadSelection) {
        record.selectStart = m_selectInfo.start;
        record.selectEnd = m_selectInfo.end;
    }
}

// 레코드로부터 상태 복원
void NemoEdit::RestoreState(const UndoRecord& record) {
    m_caretPos = record.caretPos;

    if (record.hadSelection) {
        m_selectInfo.start = record.selectStart;
        m_selectInfo.end = record.selectEnd;
        m_selectInfo.anchor = record.selectStart;
        m_selectInfo.isSelected = true;
        m_selectInfo.isSelecting = false;
    }
    else {
        m_selectInfo.start = m_selectInfo.end = m_selectInfo.anchor = m_caretPos;
        m_selectInfo.isSelected = false;
        m_selectInfo.isSelecting = false;
    }
}

// Undo 스택에 레코드 추가
void NemoEdit::AddUndoRecord(const UndoRecord& record) {
    m_undoStack.push_back(record);
    m_redoStack.clear();

    // 스택 크기 제한 (메모리 사용량 관리)
    const size_t MAX_UNDO_SIZE = 1000;
    if (m_undoStack.size() > MAX_UNDO_SIZE) {
        m_undoStack.erase(m_undoStack.begin());
    }
}

// Insert 레코드 생성
NemoEdit::UndoRecord NemoEdit::CreateInsertRecord(const TextPos& pos, const std::wstring& text) {
    UndoRecord record;
    record.type = UndoRecord::Insert;
    record.start = pos;             // 삽입할 위치 (작업 전)
    record.text = text;             // 삽입할 내용
    SaveCurrentState(record);
    return record;
}

// Delete 레코드 생성
NemoEdit::UndoRecord NemoEdit::CreateDeleteRecord(const TextPos& start, const TextPos& end, const std::wstring& text) {
    UndoRecord record;
    record.type = UndoRecord::Delete;
    record.start = start;           // 삭제 시작 위치 (작업 전)
    record.end = end;               // 삭제 끝 위치 (작업 전)
    record.text = text;             // 삭제될 내용
    SaveCurrentState(record);
    return record;
}

// Replace 레코드 생성
NemoEdit::UndoRecord NemoEdit::CreateReplaceRecord(const TextPos& start, const TextPos& end, const TextPos& startAfter, const TextPos& endAfter, const std::wstring& originalText) {
    UndoRecord record;
    record.type = UndoRecord::Replace;
    record.start = start;           // 교체 시작 위치 (작업 전)
    record.end = end;               // 교체 끝 위치 (작업 전)
    record.startAfter = startAfter; // 교체 시작 위치 (작업 후)
    record.endAfter = endAfter; // 교체 시작 위치 (작업 후)
    record.text = originalText;     // 원본 내용
    SaveCurrentState(record);
    return record;
}

// 지정된 범위의 텍스트 삭제 (Undo/Redo용)
void NemoEdit::DeleteSelectionRange(const TextPos& start, const TextPos& end) {
    if (start.lineIndex == end.lineIndex) {
        // 한 라인 내 삭제
        std::wstring text = m_rope.getLine(start.lineIndex);
        text.erase(start.column, end.column - start.column);
        m_rope.update(start.lineIndex, text);
    }
    else {
        // 여러 라인 삭제
        m_rope.eraseAt(start.lineIndex, start.column,
            m_rope.getLineSize(start.lineIndex) - start.column);
        m_rope.eraseAt(end.lineIndex, 0, end.column);
        if (end.lineIndex - start.lineIndex > 1) {
            m_rope.eraseRange(start.lineIndex + 1, end.lineIndex - start.lineIndex - 1);
        }
        m_rope.mergeLine(start.lineIndex);
    }
}

// 지정된 위치에 텍스트 삽입 (Undo/Redo용)
void NemoEdit::InsertTextAt(const TextPos& pos, std::wstring& text) {
    std::list<std::wstring> parts;
    SplitTextByNewlines(text, parts);

    if (parts.size() == 1) {
        // 단일 텍스트 삽입
        m_rope.insertAt(pos.lineIndex, pos.column, parts.front());
    }
    else {
        // 여러 라인 텍스트 삽입
        std::wstring currentLine = m_rope.getLine(pos.lineIndex);
        std::wstring headText = currentLine.substr(0, pos.column);
        std::wstring tailText = currentLine.substr(pos.column);

        // 첫 번째 라인과 마지막 라인은 기존 텍스트와 결합
        parts.front() = headText + parts.front();
        parts.back() += tailText;

        // 첫 번째 라인 업데이트
        m_rope.update(pos.lineIndex, parts.front());

        // 나머지 라인들 삽입 - SPLIT_THRESHOLD 조건 적용
        if (parts.size() - 1 > SPLIT_THRESHOLD) {
            parts.pop_front();
            m_rope.insertMultiple(pos.lineIndex + 1, parts);
        }
        else {
            // 소량 삽입 시 개별 삽입 (첫 번째 라인 제외)
            auto it = parts.begin();
            if (!parts.empty()) ++it;
            for (size_t i = 1; i < parts.size(); ++it, ++i) {
                if (it != parts.end()) {
                    m_rope.insert(pos.lineIndex + i, *it);
                }
            }
        }
    }
}

// 선택된 텍스트 클립보드로 복사
void NemoEdit::Copy() {
    if (!m_selectInfo.isSelected) return;

    // 선택 영역 순서 정규화
    TextPos start = m_selectInfo.start;
    TextPos end = m_selectInfo.end;
    if(end.lineIndex < start.lineIndex || 
       (end.lineIndex == start.lineIndex && end.column < start.column)) {
        start = m_selectInfo.end;
        end = m_selectInfo.start;
    }
    // 선택 텍스트 추출
	std::wstring selectedText = m_rope.getTextRange(start.lineIndex, start.column, end.lineIndex, end.column);

	SaveClipBoard(selectedText);
}

// 선택된 텍스트를 잘라내기 (클립보드에 복사 후 삭제)
void NemoEdit::Cut() {
    if (m_isReadOnly) return;

    // 선택 영역이 없으면 작업하지 않음
    if (!m_selectInfo.isSelected) {
        return;
    }

    try {
        // 먼저 클립보드에 복사
        Copy();

        // 선택 영역 삭제 (DeleteSelection 함수는 이미 Undo 처리를 포함하고 있음)
        DeleteSelection();
    }
    catch (...) {
        // 예외 처리: 메모리 부족 등의 오류 발생 시
        MessageBeep(MB_ICONERROR);
    }
}

// 클립보드의 텍스트를 현 캐럿 위치에 붙여넣기
void NemoEdit::Paste() {
    if (m_isReadOnly) return;

    std::wstring clipText = LoadClipText();
    if (clipText.empty()) return;

    ReplaceSelection(clipText);
}

// Undo 실행
void NemoEdit::Undo() {
    if (m_undoStack.empty() || m_isReadOnly) return;

    // 마지막 작업 기록 가져오기
    UndoRecord record = m_undoStack.back();
    m_undoStack.pop_back();

    // Redo를 위한 현재 상태 저장
    UndoRecord redoRecord;
    redoRecord.type = record.type;
    SaveCurrentState(redoRecord);

    // 작업 타입별 처리
    switch (record.type) {
    case UndoRecord::Insert: {
        // Insert 취소: 삽입된 텍스트 제거
        std::list<std::wstring> parts;
        SplitTextByNewlines(record.text, parts);

        TextPos end;
        if (parts.size() == 1) 
             end = TextPos(record.start.lineIndex + (int)parts.size() - 1, record.start.column+parts.back().length());
        else 
            end = TextPos(record.start.lineIndex + (int)parts.size() - 1, parts.back().length());
        
        DeleteSelectionRange(record.start, end);

        redoRecord.start = record.start;
        redoRecord.text = record.text;
        break;
    }

    case UndoRecord::Delete: {
        // Delete 취소: 삭제된 텍스트 복원
        redoRecord.start = record.start;
        redoRecord.end = record.end;
        redoRecord.text = record.text;

        InsertTextAt(record.start, record.text);
        break;
    }

    case UndoRecord::Replace: {
        // 현재 B 텍스트를 정확한 범위로 가져오기
        std::wstring currentText = m_rope.getTextRange(
            record.startAfter.lineIndex, record.startAfter.column,
            record.endAfter.lineIndex, record.endAfter.column);  // B의 정확한 범위!

        // Redo 레코드 생성 : Undo를 그대로 저장하자. 작업의 방향성 그대로
        redoRecord.start = record.start;
        redoRecord.end = record.end; 
        redoRecord.startAfter = record.startAfter;
        redoRecord.endAfter = record.endAfter;
        redoRecord.text = currentText;  // B 텍스트 : B가 삭제되고 A로 복원되기 때문에 지워진 정보 저장

        // B 삭제 후 A 복원
        DeleteSelectionRange(record.startAfter, record.endAfter);  // B 삭제
        InsertTextAt(record.start, record.text);          // A 복원
        break;
    }
    }

    // 상태 복원
    RestoreState(record);

    // Redo 스택에 추가
    m_redoStack.push_back(redoRecord);

    // 화면 갱신
    EnsureCaretVisible();
    RecalcScrollSizes();
    Invalidate(FALSE);
}

// Redo 실행
void NemoEdit::Redo() {
    if (m_redoStack.empty() || m_isReadOnly) return;

    // 마지막 Redo 기록 가져오기
    UndoRecord record = m_redoStack.back();
    m_redoStack.pop_back();

    // Undo를 위한 현재 상태 저장
    UndoRecord undoRecord;
    undoRecord.type = record.type;
    SaveCurrentState(undoRecord);

    // 작업 타입별 처리 (Undo와 반대)
    switch (record.type) {
    case UndoRecord::Insert: {
        // Insert 재실행
        InsertTextAt(record.start, record.text);
        undoRecord.start = record.start;
        undoRecord.text = record.text;
        break;
    }

    case UndoRecord::Delete: {
        // Delete 재실행
        undoRecord.start = record.start;
        undoRecord.end = record.end;
        undoRecord.text = record.text;
        DeleteSelectionRange(record.start, record.end);
        break;
    }

    case UndoRecord::Replace: {
        // Replace 재실행
        std::wstring originalText = m_rope.getTextRange(
            record.start.lineIndex, record.start.column,
            record.end.lineIndex, record.end.column); // A의 정보

        undoRecord.start = record.start;
        undoRecord.end = record.end;
        undoRecord.startAfter = record.startAfter;
        undoRecord.endAfter = record.endAfter;
        undoRecord.text = originalText;

        DeleteSelectionRange(record.start, record.end); // A 삭제
        InsertTextAt(record.start, record.text); // B복원
        break;
    }
    }

    // 상태 복원
    RestoreState(record);

    // Undo 스택에 추가
    m_undoStack.push_back(undoRecord);

    // 화면 갱신
    EnsureCaretVisible();
    RecalcScrollSizes();
    Invalidate(FALSE);
}

// 커서를 위/아래로 이동시키는 통합 함수 (양수: 위로, 음수: 아래로)
void NemoEdit::UpDown(int step) {
    if (step == 0) return; // 이동 없음

    TextPos oldPos = m_caretPos; // 이전 커서 위치 저장

    if (m_wordWrap) {
        // 워드랩 모드 - 워드랩된 줄 단위로 이동
        if (step > 0) {
            // 위로 이동
            for (int i = 0; i < step; i++) {
                // 현재 커서 위치의 라인과 컬럼
                int currentLine = m_caretPos.lineIndex;
                int currentColumn = m_caretPos.column;

                // 현재 라인의 워드랩 정보 가져오기
                auto wrapPositions = FindWordWrapPosition(currentLine);

                // 현재 커서가 어느 워드랩 줄에 있는지 결정
                int currentWrapLine = 0;
                int wrapStartCol = 0;

                for (size_t i = 0; i < wrapPositions.size(); i++) {
                    if (currentColumn < wrapPositions[i]) {
                        break;
                    }
                    currentWrapLine++;
                    wrapStartCol = wrapPositions[i];
                }

                // 현재 워드랩 줄 내에서의 상대적 위치
                int relativeCol = currentColumn - wrapStartCol;

                if (currentWrapLine > 0) {
                    // 같은 메인 라인의 이전 워드랩 줄로 이동
                    int prevWrapStartCol = (currentWrapLine > 1) ? wrapPositions[currentWrapLine - 2] : 0;
                    int prevWrapEndCol = wrapPositions[currentWrapLine - 1];
                    int prevWrapWidth = prevWrapEndCol - prevWrapStartCol;

                    // 같은 상대적 위치로 이동하되, 이전 워드랩 줄의 길이 제한
                    m_caretPos.column = prevWrapStartCol + min(relativeCol, prevWrapWidth);
                }
                else if (currentLine > 0) {
                    // 이전 메인 라인의 마지막 워드랩 줄로 이동
                    m_caretPos.lineIndex = currentLine - 1;
                    auto prevLineIt = m_rope.getIterator(m_caretPos.lineIndex);
                    auto prevWrapPositions = FindWordWrapPosition(m_caretPos.lineIndex);

                    if (prevWrapPositions.empty()) {
                        // 이전 라인이 워드랩 없으면 원하는 컬럼으로 이동 (길이 제한)
                        m_caretPos.column = min(oldPos.column, (int)prevLineIt->size());
                    }
                    else {
                        // 이전 라인의 마지막 워드랩 줄로 이동
                        int lastWrapStartCol = prevWrapPositions.back();
                        m_caretPos.column = lastWrapStartCol + min(relativeCol, (int)prevLineIt->size() - lastWrapStartCol);
                    }

                    // 이미 첫 줄까지 도달했으면 더 이상 이동 안함
                    if (m_caretPos.lineIndex == 0 && m_caretPos.column == 0) {
                        break;
                    }
                }
                else {
                    // 이미 첫 줄, 첫 워드랩이면 첫 컬럼으로
                    m_caretPos.column = 0;
                    break; // 더 이상 이동 불가
                }
            }
        }
        else {
            // 아래로 이동
            int absStep = -step; // 양수로 변환

            for (int i = 0; i < absStep; i++) {
                // 현재 커서 위치의 라인과 컬럼
                int currentLine = m_caretPos.lineIndex;
                int currentColumn = m_caretPos.column;
                auto currentLineIt = m_rope.getIterator(currentLine);

                // 현재 라인의 워드랩 정보 가져오기
                auto wrapPositions = FindWordWrapPosition(currentLine);

                // 현재 커서가 어느 워드랩 줄에 있는지 결정
                int currentWrapLine = 0;
                int wrapStartCol = 0;

                for (size_t i = 0; i < wrapPositions.size(); i++) {
                    if (currentColumn < wrapPositions[i]) {
                        break;
                    }
                    currentWrapLine++;
                    wrapStartCol = wrapPositions[i];
                }

                // 현재 워드랩 줄 내에서의 상대적 위치
                int relativeCol = currentColumn - wrapStartCol;

                if (currentWrapLine < (int)wrapPositions.size()) {
                    // 같은 메인 라인의 다음 워드랩 줄로 이동
                    int nextWrapStartCol = wrapPositions[currentWrapLine];
                    int nextWrapEndCol = (currentWrapLine + 1 < wrapPositions.size()) ?
                        wrapPositions[currentWrapLine + 1] : (int)currentLineIt->size();
                    int nextWrapWidth = nextWrapEndCol - nextWrapStartCol;

                    // 같은 상대적 위치로 이동하되, 다음 워드랩 줄의 길이 제한
                    m_caretPos.column = nextWrapStartCol + min(relativeCol, nextWrapWidth);
                }
                else if (currentLine < (int)m_rope.getSize() - 1) {
                    // 다음 메인 라인의 첫 워드랩 줄로 이동
                    m_caretPos.lineIndex = currentLine + 1;
                    auto nextLineIt = m_rope.getIterator(m_caretPos.lineIndex);

                    // 다음 라인의 첫 워드랩 줄의 길이만큼 상대적 위치 제한
                    auto nextWrapPositions = FindWordWrapPosition(m_caretPos.lineIndex);
                    int nextWrapEndCol = nextWrapPositions.empty() ? (int)nextLineIt->size() : nextWrapPositions[0];

                    m_caretPos.column = min(relativeCol, nextWrapEndCol);

                    // 이미 마지막 줄까지 도달했으면 더 이상 이동 안함
                    if (m_caretPos.lineIndex == (int)m_rope.getSize() - 1 && m_caretPos.column == (int)nextLineIt->size()) {
                        break;
                    }
                }
                else {
                    // 이미 마지막 줄, 마지막 워드랩이면 라인 끝으로
                    m_caretPos.column = (int)currentLineIt->size();
                    break; // 더 이상 이동 불가
                }
            }
        }
    }
    else {
        // 일반 모드 - 라인 단위로 이동
        if (step > 0) {
            // 위로 이동
            for (int i = 0; i < step; i++) {
                if (m_caretPos.lineIndex > 0) {
                    m_caretPos.lineIndex--;
                    int desiredCol = oldPos.column;
                    int lineSize = (int)m_rope.getLineSize(m_caretPos.lineIndex);
                    if (m_caretPos.lineIndex<m_rope.getSize() && desiredCol >lineSize) {
                        desiredCol = lineSize;
                    }
                    m_caretPos.column = desiredCol;
                }
                else {
                    // 이미 첫 라인이면 첫 컬럼으로만 이동
                    m_caretPos.column = 0;
                    break;
                }
            }
        }
        else {
            // 아래로 이동
            int absStep = -step; // 양수로 변환

            for (int i = 0; i < absStep; i++) {
                if (m_caretPos.lineIndex < (int)m_rope.getSize() - 1) {
                    m_caretPos.lineIndex++;
                    int desiredCol = oldPos.column;
                    int lineSize = (int)m_rope.getLineSize(m_caretPos.lineIndex);
                    if (m_caretPos.lineIndex<m_rope.getSize() && desiredCol >lineSize) {
                        desiredCol = lineSize;
                    }
                    m_caretPos.column = desiredCol;
                }
                else {
                    // 이미 마지막 라인이면 라인 끝으로만 이동
                    int lineSize = (int)m_rope.getLineSize(m_caretPos.lineIndex);
                    m_caretPos.column = (m_caretPos.lineIndex < m_rope.getSize() ? lineSize : 0);
                    break;
                }
            }
        }
    }
}

// 텍스트 분할
void NemoEdit::SplitTextByNewlines(std::wstring& text, std::list<std::wstring>& parts) {
    parts.clear();

    size_t length = text.size();
    size_t i = 0;
    size_t start = 0;
    const wchar_t* data = text.data();

    // 단일 루프: \r\n, \r, \n 처리
    while (i < length) {
        if (data[i] == L'\r' || data[i] == L'\n') {
            // \r\n 쌍 처리
            if (data[i] == L'\r' && i + 1 < length && data[i + 1] == L'\n') {
                if (i > start) {
                    parts.push_back(text.substr(start, i - start));
                }
                else {
                    parts.push_back(L"");
                }
                start = i + 2;
                i += 2;
            }
            else {
                // 독립된 \r 또는 \n
                if (i > start) {
                    parts.push_back(text.substr(start, i - start));
                }
                else {
                    parts.push_back(L"");
                }
                start = i + 1;
                i++;
            }
        }
        else {
            i++;
        }
    }

    // 마지막 부분 처리
    if (start < length) {
        parts.push_back(text.substr(start));
    }

    // 텍스트가 비어 있거나 \n 또는 \r로 끝나는 경우 빈 줄 추가
    if (parts.empty() || (length > 0 && (data[length - 1] == L'\n' || data[length - 1] == L'\r'))) {
        parts.push_back(L"");
    }
}

// 여러 줄 선택 시 탭 추가 처리 메서드
void NemoEdit::AddTabToSelectedLines() {
    if (!m_selectInfo.isSelected) return;

    // 선택 영역의 시작과 끝 정규화
    TextPos start = m_selectInfo.start;
    TextPos end = m_selectInfo.end;
    if (end.lineIndex < start.lineIndex ||
        (end.lineIndex == start.lineIndex && end.column < start.column)) {
        start = m_selectInfo.end;
        end = m_selectInfo.start;
    }

    // 한 줄만 선택된 경우 일반 탭 처리
    if (start.lineIndex == end.lineIndex) return;

    // 변경될 전체 영역의 원본 텍스트 저장 (작업 전 상태)
    TextPos recordStart(start.lineIndex, 0);
    TextPos recordEnd(end.lineIndex, m_rope.getLineSize(end.lineIndex)); // 변경후의 사이즈를 넣는다.
    std::wstring originalText = m_rope.getTextRange(
        recordStart.lineIndex, recordStart.column,
        recordEnd.lineIndex, recordEnd.column);

    // Undo 레코드 생성
    UndoRecord record = CreateReplaceRecord(recordStart, recordEnd, recordStart, recordEnd, originalText);

    // 각 라인에 탭 추가
    std::wstring tabStr(1, L'\t');
    for (int lineIdx = start.lineIndex; lineIdx <= end.lineIndex; lineIdx++) {
        std::wstring line = m_rope.getLine(lineIdx);
        line.insert(0, tabStr);
        m_rope.update(lineIdx, line);
    }

    record.endAfter.column = m_rope.getLineSize(end.lineIndex);  // 텝 추가 적용

    // 선택 영역 위치 조정 (탭 추가로 인한 변화)
    TextPos newStart = start;
    TextPos newEnd = end;
    newStart.column += 1;
    newEnd.column += 1;

    // 선택 영역 및 캐럿 위치 업데이트
    m_selectInfo.start = newStart;
    m_selectInfo.end = newEnd;
    m_selectInfo.anchor = newStart;
    m_caretPos = newEnd;

    // Undo 스택에 추가
    AddUndoRecord(record);

    // 화면 갱신
    EnsureCaretVisible();
    Invalidate(FALSE);
}

// 여러 줄 선택 시 탭 제거 처리 메서드 (Shift+Tab용)
void NemoEdit::RemoveTabFromSelectedLines() {
    if (!m_selectInfo.isSelected) return;

    // 선택 영역의 시작과 끝 정규화
    TextPos start = m_selectInfo.start;
    TextPos end = m_selectInfo.end;
    if (end.lineIndex < start.lineIndex ||
        (end.lineIndex == start.lineIndex && end.column < start.column)) {
        start = m_selectInfo.end;
        end = m_selectInfo.start;
    }

    // 한 줄만 선택된 경우 일반 처리
    if (start.lineIndex == end.lineIndex) return;

    // 변경될 전체 영역의 원본 텍스트 저장 (작업 전 상태)
    TextPos recordStart(start.lineIndex, 0);
    TextPos recordEnd(end.lineIndex, m_rope.getLineSize(end.lineIndex));
    std::wstring originalText = m_rope.getTextRange(
        recordStart.lineIndex, recordStart.column,
        recordEnd.lineIndex, recordEnd.column);

    // 변경 사항이 있는지 미리 확인
    bool anyChanged = false;
    std::vector<int> removedCounts(end.lineIndex - start.lineIndex + 1, 0);

    for (int lineIdx = start.lineIndex; lineIdx <= end.lineIndex; lineIdx++) {
        std::wstring line = m_rope.getLine(lineIdx);
        if (!line.empty()) {
            if (line[0] == L'\t') {
                removedCounts[lineIdx - start.lineIndex] = 1;
                anyChanged = true;
            }
            else if (line[0] == L' ') {
                int spacesToRemove = 0;
                for (int i = 0; i < m_tabSize && i < line.length() && line[i] == L' '; i++) {
                    spacesToRemove++;
                }
                if (spacesToRemove > 0) {
                    removedCounts[lineIdx - start.lineIndex] = spacesToRemove;
                    anyChanged = true;
                }
            }
        }
    }

    // 변경된 사항이 없으면 종료
    if (!anyChanged) return;

    // Undo 레코드 생성
    UndoRecord record = CreateReplaceRecord(recordStart, recordEnd, recordStart, recordEnd, originalText);

    // 각 라인에서 들여쓰기 제거
    for (int lineIdx = start.lineIndex; lineIdx <= end.lineIndex; lineIdx++) {
        int removeCount = removedCounts[lineIdx - start.lineIndex];
        if (removeCount > 0) {
            std::wstring line = m_rope.getLine(lineIdx);
            line.erase(0, removeCount);
            m_rope.update(lineIdx, line);
        }
    }

    record.endAfter.column = m_rope.getLineSize(end.lineIndex); // 변경 후의 끝점 수정 : Shift+Tab 최종 사이즈 적용

    // 선택 영역 위치 조정
    TextPos newStart = start;
    TextPos newEnd = end;
    if (newStart.column > 0) {
        newStart.column = max(0, newStart.column - removedCounts[0]);
    }
    if (newEnd.column > 0) {
        newEnd.column = max(0, newEnd.column - removedCounts[end.lineIndex - start.lineIndex]);
    }

    // 선택 영역 및 캐럿 위치 업데이트
    m_selectInfo.start = newStart;
    m_selectInfo.end = newEnd;
    m_selectInfo.anchor = newStart;
    m_caretPos = newEnd;

    // Undo 스택에 추가
    AddUndoRecord(record);

    // 화면 갱신
    EnsureCaretVisible();
    Invalidate(FALSE);
}

// 새로운 문자를 현 위치에 삽입
void NemoEdit::InsertChar(wchar_t ch) {
    if (m_isReadOnly) return;

    // 개행 문자 처리
    if (ch == L'\r' || ch == L'\n') {
        InsertNewLine();
        return;
    }
    
    std::wstring addStr(1, ch);
    
    // 선택영역이 있으면 Replace로 가기때문에 여기 오지 않음.
    
    // Undo 레코드 생성 (작업 전 상태)
    UndoRecord record = CreateInsertRecord(m_caretPos, addStr);
    
    // 실제 삽입 작업
    m_rope.insertAt(m_caretPos.lineIndex, m_caretPos.column, addStr);
    
    // 캐럿 이동
    m_caretPos.column++;
    
    // Undo 스택에 추가
    AddUndoRecord(record);
}

// 새 줄 삽입 (현재 위치에서 줄 분리)
void NemoEdit::InsertNewLine() {
    if (m_isReadOnly) return;

    // 선택 영역이 있으면 먼저 삭제
    //if (m_selectInfo.isSelected) {
    //    DeleteSelection();
    //}

    // Undo 레코드 생성 (작업 전 상태)
    UndoRecord record = CreateInsertRecord(m_caretPos, L"\n");

    std::wstring text = m_rope.getLine(m_caretPos.lineIndex);
    std::wstring tail = text.substr(m_caretPos.column);
    std::wstring chgText = text.substr(0, m_caretPos.column);

    m_rope.update(m_caretPos.lineIndex, chgText);
    m_rope.insert(m_caretPos.lineIndex + 1, tail);

    // 캐럿 위치 업데이트
    m_caretPos.lineIndex += 1;
    m_caretPos.column = 0;
    m_scrollX = 0;

    // Undo 스택에 추가
    AddUndoRecord(record);
}

// 문자 삭제 (backspace=true인 경우 Backspace 처리, false이면 Delete 처리)
void NemoEdit::DeleteChar(bool backspace) {
    if (m_isReadOnly) return;

    // 선택 영역이 있을 경우 해당 영역 삭제
    if (m_selectInfo.isSelected) {
        DeleteSelection();
        return;
    }

    UndoRecord record;

    if (backspace) {
        // 백스페이스 처리
        if (m_caretPos.column == 0) {
            // 라인 맨 앞에서 백스페이스 -> 이전 라인과 병합
            if (m_caretPos.lineIndex > 0) {
                int prevLength = (int)m_rope.getLineSize(m_caretPos.lineIndex - 1);

                // Undo 레코드 생성 (작업 전 상태)
                TextPos deleteStart(m_caretPos.lineIndex - 1, prevLength);
                TextPos deleteEnd(m_caretPos.lineIndex, 0);
                record = CreateDeleteRecord(deleteStart, deleteEnd, L"\n");

                // 실제 병합 작업
                m_rope.mergeLine(m_caretPos.lineIndex - 1);

                // 캐럿 위치 업데이트
                m_caretPos.lineIndex--;
                m_caretPos.column = prevLength;
            }
            else {
                return; // 첫 라인 첫 위치에서는 백스페이스 무시
            }
        }
        else {
            // 일반 문자 삭제
            std::wstring line = m_rope.getLine(m_caretPos.lineIndex);
            std::wstring removeStr = line.substr(m_caretPos.column - 1, 1);

            // Undo 레코드 생성 (작업 전 상태)
            TextPos deleteStart(m_caretPos.lineIndex, m_caretPos.column - 1);
            TextPos deleteEnd(m_caretPos.lineIndex, m_caretPos.column);
            record = CreateDeleteRecord(deleteStart, deleteEnd, removeStr);

            // 실제 삭제 작업
            m_rope.eraseAt(m_caretPos.lineIndex, m_caretPos.column - 1, 1);
            m_caretPos.column--;
        }
    }
    else {
        // Delete 키 처리
        if (m_caretPos.column >= (int)m_rope.getLineSize(m_caretPos.lineIndex)) {
            // 라인 끝에서 Delete -> 다음 라인과 병합
            if (m_caretPos.lineIndex < (int)m_rope.getSize() - 1) {
                // Undo 레코드 생성 (작업 전 상태)
                TextPos deleteStart = m_caretPos;
                TextPos deleteEnd(m_caretPos.lineIndex + 1, 0);
                record = CreateDeleteRecord(deleteStart, deleteEnd, L"\n");

                // 실제 병합 작업
                m_rope.mergeLine(m_caretPos.lineIndex);
            }
            else {
                return; // 마지막 라인 끝에서는 Delete 무시
            }
        }
        else {
            // 일반 문자 삭제
            std::wstring line = m_rope.getLine(m_caretPos.lineIndex);
            std::wstring removeStr = line.substr(m_caretPos.column, 1);

            // Undo 레코드 생성 (작업 전 상태)
            TextPos deleteStart = m_caretPos;
            TextPos deleteEnd(m_caretPos.lineIndex, m_caretPos.column + 1);
            record = CreateDeleteRecord(deleteStart, deleteEnd, removeStr);

            // 실제 삭제 작업
            m_rope.eraseAt(m_caretPos.lineIndex, m_caretPos.column, 1);
        }
    }

    // Undo 스택에 추가
    AddUndoRecord(record);
}

void NemoEdit::CancelSelection() {
    m_selectInfo.isSelected = false;
	m_selectInfo.isSelecting=false;
	m_selectInfo.start.lineIndex = m_selectInfo.end.lineIndex = m_caretPos.lineIndex;
    m_selectInfo.start.column = m_selectInfo.end.column = m_caretPos.column;
}

// 선택 영역의 텍스트 삭제
void NemoEdit::DeleteSelection() {
    if (!m_selectInfo.isSelected || m_isReadOnly) return;

    // 선택 영역 정규화
    TextPos start = m_selectInfo.start;
    TextPos end = m_selectInfo.end;
    if (end.lineIndex < start.lineIndex ||
        (end.lineIndex == start.lineIndex && end.column < start.column)) {
        start = m_selectInfo.end;
        end = m_selectInfo.start;
    }

    // 삭제될 텍스트 미리 저장 (작업 전 상태)
    std::wstring removedText = m_rope.getTextRange(
        start.lineIndex, start.column, end.lineIndex, end.column);

    // Undo 레코드 생성 (작업 전 상태)
    UndoRecord record = CreateDeleteRecord(start, end, removedText);

    // 실제 삭제 작업
    if (start.lineIndex == end.lineIndex) {
        // 한 라인 내 삭제
        std::wstring text = m_rope.getLine(start.lineIndex);
        text.erase(start.column, end.column - start.column);
        m_rope.update(start.lineIndex, text);
    }
    else {
        // 여러 라인 삭제
        m_rope.eraseAt(start.lineIndex, start.column,
            m_rope.getLineSize(start.lineIndex) - start.column);
        m_rope.eraseAt(end.lineIndex, 0, end.column);
        if (end.lineIndex - start.lineIndex > 1) {
            m_rope.eraseRange(start.lineIndex + 1, end.lineIndex - start.lineIndex - 1);
        }
        m_rope.mergeLine(start.lineIndex);
    }

    // 캐럿 위치 및 선택 영역 업데이트
    m_caretPos = start;
    m_selectInfo.start = m_selectInfo.end = m_selectInfo.anchor = m_caretPos;
    m_selectInfo.isSelected = false;
    m_selectInfo.isSelecting = false;

    // Undo 스택에 추가
    AddUndoRecord(record);

    // 빈 문서 처리
    if (m_rope.empty()) {
        m_rope.insertBack(L"");
        m_caretPos = TextPos(0, 0);
        m_selectInfo.start = m_selectInfo.end = m_selectInfo.anchor = m_caretPos;
    }
}

void NemoEdit::ReplaceSelection(std::wstring text) {
    if (m_isReadOnly) return;

    UndoRecord record;
    if (m_selectInfo.isSelected) {
        // 선택 영역 정규화
        TextPos start = m_selectInfo.start;
        TextPos end = m_selectInfo.end;
        if (end.lineIndex < start.lineIndex ||
            (end.lineIndex == start.lineIndex && end.column < start.column)) {
            start = m_selectInfo.end;
            end = m_selectInfo.start;
        }

        // 원본 A 텍스트 저장
        std::wstring originalText = m_rope.getTextRange(
            start.lineIndex, start.column, end.lineIndex, end.column);

        // B의 끝 위치 미리 계산
        std::list<std::wstring> parts;
        SplitTextByNewlines(text, parts);

        TextPos bEndPos;
        if (parts.size() == 1) {
            bEndPos = TextPos(start.lineIndex, start.column + text.length());
        }
        else {
            bEndPos = TextPos(start.lineIndex + parts.size() - 1, parts.back().length());
        }

        // 핵심: UndoRecord의 end를 B의 끝 위치로 저장
        record = CreateReplaceRecord(start, end, start, bEndPos, originalText);

        // 실제 교체 작업
        DeleteSelectionRange(start, end);  // A 삭제
        InsertTextAt(start, text);         // B 삽입

        // 캐럿 위치 설정
        m_caretPos = bEndPos;

        // 선택 영역 해제
        m_selectInfo.start = m_selectInfo.end = m_selectInfo.anchor = m_caretPos;
        m_selectInfo.isSelected = false;
        m_selectInfo.isSelecting = false;
    }
    else {
        // 캐럿 위치 유효성 검사
        if (m_caretPos.lineIndex < 0)
            m_caretPos.lineIndex = 0;
        if (m_caretPos.lineIndex >= (int)m_rope.getSize())
            m_caretPos.lineIndex = (int)m_rope.getSize() - 1;

        // Insert 타입의 Undo 레코드 생성
        record = CreateInsertRecord(m_caretPos, text);

        // 텍스트 삽입
        InsertTextAt(m_caretPos, text);

        // 캐럿 위치 계산 (삽입된 텍스트의 끝)
        std::list<std::wstring> parts;
        SplitTextByNewlines(text, parts);

        if (parts.size() == 1) {
            m_caretPos.column += text.length();
        }
        else {
            m_caretPos.lineIndex += parts.size() - 1;
            m_caretPos.column = parts.back().length();
        }
    }

    AddUndoRecord(record);

    EnsureCaretVisible();
    RecalcScrollSizes();
    Invalidate(FALSE);
}

// 최적화된 라인 너비 계산
int NemoEdit::GetTextWidth(const std::wstring& line) {
    int width= m_d2Render.GetTextWidth(line);
    if (width > m_maxWidth) m_maxWidth = width;
    return width;
}

// lineIndex: 라인 인덱스 - 다음줄이 시작되는 column의 위치들이 데이터에 저장
std::vector<int> NemoEdit::FindWordWrapPosition(int lineIndex) {
    std::vector<int> wrapPos;

    std::wstring lineText = m_rope.getLine(lineIndex);
    if (lineText.empty()) return {}; // 빈 줄일 경우 워드랩 필요 없음

    const size_t LARGE_TEXT_THRESHOLD = 2048;

    // 대용량 텍스트 처리 (1024자 이상)
    if (lineText.length() > LARGE_TEXT_THRESHOLD) {
        // 처음 200문자 샘플로 화면에 들어갈 수 있는 최대 문자 수 찾기
        int sampleSize = min(200, (int)lineText.length());
        std::wstring sampleText = lineText.substr(0, sampleSize);

        int low = 1;
        int high = sampleSize;
        int maxCharsInLine = 1;

        while (low <= high) {
            int mid = (low + high) / 2;

            std::wstring testText = sampleText.substr(0, mid);
            std::wstring tabTestText = ExpandTabs(testText);
            int testWidth = GetTextWidth(tabTestText);

            if (testWidth <= m_wordWrapWidth) {
                maxCharsInLine = mid;
                low = mid + 1;
            }
            else {
                high = mid - 1;
            }
        }

        // 찾은 최대 문자 수에서 5 빼기
        int charsPerLine = maxCharsInLine - 10;
        if (charsPerLine < 10) charsPerLine = 10; // 최소 10문자 보장

        // 고정 크기로 분할
        for (size_t pos = charsPerLine; pos < lineText.length(); pos += charsPerLine) {
            wrapPos.push_back(min(pos, lineText.length()));
        }

        return wrapPos;
    }

    // 기존 로직 (1024자 미만)
    int lineWidth = GetTextWidth(ExpandTabs(lineText));
    if (lineWidth <= m_wordWrapWidth) {
        return {};
    }

    int currentPos = 0;
    int currWidthSum = 0;
    std::wstring tabText;
    int low, high, result, currWidth, mid, testSize;

    while (currentPos < (int)lineText.length()) {
        // 이진 검색으로 현재 위치에서 가장 긴 텍스트 찾기
        low = 1;
        high = (int)lineText.length() - currentPos;
        result = 1; // 기본값
        currWidth = 0;

        while (low <= high) {
            mid = (low + high) / 2;
            if (mid <= 0) mid = 1; // 보호 코드

            tabText = ExpandTabs(lineText.substr(currentPos, mid));
            testSize = GetTextWidth(tabText);

            if (testSize < m_wordWrapWidth) {
                currWidth = testSize;
                result = mid;
                low = mid + 1;
            }
            else {
                high = mid - 1;
            }
        }

        currWidthSum += currWidth;
        currentPos += result;
        wrapPos.push_back(currentPos);

        // 줄이 끝나면 종료
        if (lineWidth - currWidthSum < m_wordWrapWidth) break;
    }

    return wrapPos;
}

// 화면 좌표로부터 텍스트 캐럿 위치 계산
// ---------------------------------------------
// 일반 : 스크롤 오프셋을 고려하여 계산
// 워드랩 : m_wrapInfo를 사용하여 계산
TextPos NemoEdit::GetTextPosFromPoint(CPoint pt) {
    TextPos pos;

    if (m_wordWrap) {
        // 수직 위치 계산
        CRect client;
        GetClientRect(&client);
		int visibleLines = client.Height() / m_lineHeight;
        int lineSize = (int)m_rope.getSize();
        int prevY = 0;
        std::vector<int> wrapCols;
        int wrapColsSize = 0;
        int startCol = 0;
        int totalPrevLines = -m_scrollYWrapLine;
        for (int i = m_scrollYLine; i < lineSize; i++) {
            wrapCols = FindWordWrapPosition(i);
            totalPrevLines += (int)wrapCols.size() + 1;
            prevY = totalPrevLines * m_lineHeight + m_margin.top - m_lineSpacing/2;
            pos.lineIndex = i;
            if (prevY >= pt.y) {
                pos.lineIndex = i;
                // wrapLineIndex 계산
                wrapColsSize = (int)wrapCols.size();
                prevY -= wrapColsSize * m_lineHeight;
				if (prevY >= pt.y) {
					// 현재 줄의 wordwrap 시작 컬럼을 찾는다.
					startCol = 0;
					break;
				}
                for (int j = 0; j < wrapColsSize; j++) {
                    if (prevY + m_lineHeight *( j+1) >= pt.y) {
                        // 현재 줄의 wordwrap 시작 컬럼을 찾는다.
                        startCol = wrapCols[j];
                        break;
                    }
                }
                break;
            }
			if (totalPrevLines > visibleLines) break;
        }

        // 수평 위치 계산
        std::wstring lineText = m_rope.getLine(pos.lineIndex);
        std::wstring tabText;
        int col = 0;
        int low, high, result, pointX, mid, testSize;
        if (!lineText.empty()) {
            // 이진 검색으로 텍스트에서 현재 위치 찾기
            low = 1;
            high = (int)lineText.size() - startCol;
            result = 0; // 기본값
            pointX = pt.x - CalculateNumberAreaWidth()-m_margin.left;

            while (low <= high) {
                mid = (low + high) / 2;
                if (mid <= 0) mid = 1; // 보호 코드

                tabText = ExpandTabs(lineText.substr(startCol, mid));
                testSize = GetTextWidth(tabText);
                testSize -= GetTextWidth(tabText.substr(tabText.size() - 1, 1)) / 2;

                if (testSize < pointX) {
                    // 더 많은 텍스트를 포함할 수 있음
                    result = mid;
                    low = mid + 1;
                }
                else {
                    // 너무 많음 -> 줄이기
                    high = mid - 1;
                }
            }

            col = startCol + result;
        }
        pos.column = col;
    }
    else {
        // 수직 위치 계산
        pos.lineIndex = m_scrollYLine + (pt.y - m_margin.top - m_lineSpacing / 2) / m_lineHeight;
        if (pos.lineIndex < 0) pos.lineIndex = 0;
        else if (pos.lineIndex >= (int)m_rope.getSize()) pos.lineIndex = (int)m_rope.getSize() - 1;
        // 수평 위치 계산
        std::wstring lineText = m_rope.getLine(pos.lineIndex);
        int col = 0;

        if (!lineText.empty()) {
            // 마우스 클릭 위치에서 오프셋 계산
            int targetX = pt.x;
            if (m_showLineNumbers) {
                targetX -= CalculateNumberAreaWidth() + m_margin.left;
            }
            targetX += m_scrollX; // 가로 스크롤 오프셋 적용

            // 워드랩과 동일한 이진 검색 방식
            int low = 1;
            int high = (int)lineText.size();
            int result = 0;
            int mid, testSize;
            std::wstring tabText;

            while (low <= high) {
                mid = (low + high) / 2;
                if (mid <= 0) mid = 1; // 보호 코드

                tabText = ExpandTabs(lineText.substr(0, mid));
                testSize = GetTextWidth(tabText);
                testSize -= GetTextWidth(tabText.substr(tabText.size() - 1, 1)) / 2;

                if (testSize < targetX) {
                    result = mid;
                    low = mid + 1;
                }
                else {
                    high = mid - 1;
                }
            }

            col = result;
        }

        pos.column = col;
    }

    return TextPos(pos.lineIndex, pos.column);
}

// 캐럿 위치를 실제 화면 좌표로 업데이트 : 캐럿이 안보이면 숨긴다.
void NemoEdit::UpdateCaretPosition() {
    if(::GetFocus() != m_hWnd) return;
    CPoint pt = GetCaretPixelPos(m_caretPos);
    CRect client;
    GetClientRect(&client);
	int screenWidth = client.Width() - m_margin.left - m_margin.right - CalculateNumberAreaWidth();
	int screenHeight = client.Height() - m_margin.top - m_margin.bottom;

    // 라인 번호 영역의 동적 계산된 너비 가져오기
    int numberAreaWidth = CalculateNumberAreaWidth();

	// pt.x가 m_numberAreaWidth보다 작을 경우 스크롤이 필요함
	if (pt.x < 0 || pt.x >  screenWidth || pt.y < 0 || pt.y > screenHeight) {
		if (m_caretVisible == true) {
			::HideCaret(m_hWnd);
			m_caretVisible = false;
		}
	} else {
		pt.x += numberAreaWidth + m_margin.left;
		pt.y += m_margin.top;

        ::SetCaretPos(pt.x, pt.y);
        ::ShowCaret(m_hWnd);
        m_caretVisible = true;
    }
}

// 주어진 텍스트 위치의 픽셀 좌표 계산 : 화면좌표 ( lineIndex와 column을 받아서 좌표로 계산한 후에 반환 )
// Margin이 적용된 화면 좌표계에서 Scrolling을 적용하여 x,y 좌표를 계산 ( 이것을 받아서 -값이거나 화면  크기와 비교해서 크면 화면을 벗어난 것으로 한다. )
// 좌표는 워드랩이 아닐 경우 스크롤 오프셋을 기준으로 계산
// 워드랩일 경우 m_wrapInfo를 사용하여 계산하고 y는 스크린을 넘어갈 경우에 이전줄이나 다음줄로 표시 ( 이 함수를 호출한 부분에서 필요하면 정확하게 계산한다. )
//                       X 좌표는 정확하게 계산, Y좌표는 화면을 넘어가면 이전줄 또는 다음줄로 표시
//                      margin을 적용하여 화면의 크기에 적용하여 계산
//                       X는 스크롤 되어있는만큼 뺀다. Y는 스크롤 되어있는만큼 뺀다.
CPoint NemoEdit::GetCaretPixelPos(const TextPos& pos) {
    CPoint pt(0, 0);
    int lineIndex = pos.lineIndex;
    if (lineIndex < 0) lineIndex = 0;
    if (lineIndex >= (int)m_rope.getSize()) lineIndex = (int)m_rope.getSize() - 1;

    // 워드랩 모드일 때 다른 계산 방식 사용
    if (m_wordWrap) {
        // 화면 표시 영역 크기 계산
        CRect client;
        GetClientRect(&client);
        int screenWidth = client.Width() - m_margin.left - m_margin.right - CalculateNumberAreaWidth();
        int screenHeight = client.Height() - m_margin.top - m_margin.bottom;
        int visibleLines = screenHeight / m_lineHeight; // 마진을 제외한 화면에 보이는 줄 수 ( top margin만 계산 )
        m_wordWrapWidth = screenWidth;

        // 현재 라인의 몇 번째 워드랩 라인인지 계산
        int wrapLineIndex = 0; // 캐럿 위치가 속한 워드랩 라인 인덱스
        int startCol = 0; // 현재 워드랩 라인의 시작 컬럼
        std::vector<int> wrapPositions = FindWordWrapPosition(lineIndex);
        for (size_t i = 0; i < wrapPositions.size(); i++) {
            if (pos.column < wrapPositions[i]) {
                break;
            }
            startCol = wrapPositions[i];
            wrapLineIndex++;
        }

        // 화면 위에 있을 경우
        if (lineIndex < m_scrollYLine ||
            (lineIndex == m_scrollYLine && wrapLineIndex < m_scrollYWrapLine)) {
            pt.y = -m_lineHeight;
        }
        // 화면 밑에 있을 경우 : 줄수가 화면에 넘어가는 것의 계산 패싱을 위해 ( 원거리 계산을 줄이기 위해 )
        else if (lineIndex > m_scrollYLine + visibleLines) {
            pt.y = screenHeight + m_lineHeight;
        }
        // 화면에 보이는 라인일 경우 : 워드랩으로 화면을 넘어갈 수 있음
        else {
            // 수직 위치 : 현재 캐럿 라인 전까지의 전체 줄 수 계산
            int totalLines = -m_scrollYWrapLine;
            for (int i = m_scrollYLine; i < lineIndex; i++) {
                totalLines += (int)FindWordWrapPosition(i).size() + 1;
                if (totalLines > visibleLines) break;
            }
            totalLines += wrapLineIndex;

            // 화면 밑에 있을 경우 : 이전 줄수가 화면 최대 줄수와 같거나 초과한 경우
            if (totalLines >= visibleLines) {
                pt.y = screenHeight + m_lineHeight;
            }
            // 화면 안에 있을 경우
            else {
                pt.y = totalLines * m_lineHeight; // 이전 줄수 * 라인 높이 = 현재 줄 수 위치
            }
        }

        // 수평 위치: 해당 라인의 문자 폭 계산
        auto line = m_rope.getLine(lineIndex);
        if (!line.empty() && pos.column >= startCol) {
            if (pos.column == startCol) {
                pt.x = 0;
            }
            else {
                std::wstring text = line.substr(startCol, pos.column - startCol);
                text = ExpandTabs(text);
                pt.x = GetTextWidth(text);
            }
        }
        else {
            pt.x = 0;
        }
    }
    else {
        // 비워드랩 모드
        // 수직 위치 (스크롤 오프셋 고려)
        // 단순 코어 좌표만 만들어서 리턴.
        pt.y = (lineIndex - m_scrollYLine) * m_lineHeight;
        pt.x = 0;

        // 수평 위치: 해당 라인의 문자 폭 계산
        auto line = m_rope.getLine(lineIndex);
        if (!line.empty()) {
            if (pos.column > 0) {
                std::wstring text = line.substr(0, pos.column);
                text = ExpandTabs(text);
                pt.x = GetTextWidth(text);
            }
        }
        pt.x -= m_scrollX;
    }

    return pt;
}

// 캐럿이 보이도록 스크롤 조정 : 입력이나 이동이 있을 경우 호출, 캐럿이 보이지 않으면 스크롤 조정
void NemoEdit::EnsureCaretVisible() {
    CRect client;
    GetClientRect(&client);
    if (client.Width() <= 0 || client.Height() <= 0) return; // 화면이 없을 경우

    CPoint pt = GetCaretPixelPos(m_caretPos);
    int screenWidth = client.Width() - m_margin.left - m_margin.right - CalculateNumberAreaWidth();
    int screenHeight = client.Height() - m_margin.top - m_margin.bottom;
    int visibleLines = screenHeight / m_lineHeight; // 마진을 제외한 화면에 보이는 줄 수 ( top margin만 계산 )

    // 워드랩 모드
    if (m_wordWrap) {
        m_wordWrapWidth = screenWidth;

        // 현재 캐럿의 워드랩 라인 인덱스 계산
        std::vector<int> caretWrapPositions = FindWordWrapPosition(m_caretPos.lineIndex);
        int caretWrapLineIndex = 0; // 캐럿 위치가 속한 워드랩 라인 인덱스
        for (size_t i = 0; i < caretWrapPositions.size(); i++) {
            if (m_caretPos.column < caretWrapPositions[i]) {
                break;
            }
            caretWrapLineIndex++;
        }

        // 캐럿이 화면 위로 벗어남
        if (pt.y < 0) {
            m_scrollYLine = m_caretPos.lineIndex;
            m_scrollYWrapLine = caretWrapLineIndex;
        }
        // 캐럿이 화면 아래로 벗어남
        else if (pt.y+ m_lineHeight >= screenHeight) {
            // 캐럿을 화면 마지막 줄로 두고 거기서 역산
            int targetLine = m_caretPos.lineIndex;
            int targetWrapLine = caretWrapLineIndex;
            int remainingLines = visibleLines - 1; // 캐럿 라인 제외

            while (remainingLines > 0 && (targetLine > 0 || targetWrapLine > 0)) {
                if (targetWrapLine > 0) {
                    targetWrapLine--;
                    remainingLines--;
                }
                else {
                    targetLine--;
                    if (targetLine >= 0) {
                        std::vector<int> wrapPos = FindWordWrapPosition(targetLine);
                        targetWrapLine = (int)wrapPos.size();
                        remainingLines--;
                    }
                }
            }

            m_scrollYLine = targetLine;
            m_scrollYWrapLine = targetWrapLine;
        }
    }
    else {
        // 기존 비워드랩 모드 처리
        int inc;

        if (pt.x < 0) {
            m_scrollX = max(0, m_scrollX+pt.x);
        }
        else if (pt.x+ m_imeWidth > screenWidth) {
            inc = pt.x+ m_imeWidth - screenWidth;
            m_scrollX += inc;
        }

        // pt.y가 client.Height()보다 클 경우 스크롤이 필요함
        if (pt.y < 0) {
            m_scrollYLine = m_caretPos.lineIndex;
        }
        else if (pt.y + m_lineHeight >= screenHeight) {
            // 캐럿을 화면 마지막 줄로 설정
            m_scrollYLine = m_caretPos.lineIndex - visibleLines + 1;
            if (m_scrollYLine < 0) m_scrollYLine = 0;
        }
    }

    m_scrollYLine = max(0, m_scrollYLine);
    NemoSetScrollPos(SB_HORZ, m_scrollX, TRUE);
    NemoSetScrollPos(SB_VERT, m_scrollYLine, TRUE);
    UpdateCaretPosition();
    RecalcScrollSizes();
}

void NemoEdit::ScrollViewBy(int pageCount, int lineCount) {
    CRect client;
    GetClientRect(&client);
    int visibleLines = max(1, (client.Height() - m_margin.top - m_margin.bottom) / m_lineHeight);

    // 총 이동할 라인 수 계산
    int totalMoveLines = (pageCount * visibleLines) + lineCount;

    // 페이지 이동일 때는 캐럿도 함께 이동
    bool moveCaretToo = (pageCount != 0);

    if (m_wordWrap) {
        // === WordWrap 모드 ===
        int currentLine = m_scrollYLine;
        int currentWrapLine = m_scrollYWrapLine;
        int totalLines = (int)m_rope.getSize();

        if (totalMoveLines > 0) {
            // 아래로 이동
            for (int i = 0; i < totalMoveLines; i++) {
                if (currentLine >= totalLines - 1) break;

                std::vector<int> wrapPositions = FindWordWrapPosition(currentLine);
                int maxWrapLines = (int)wrapPositions.size();

                if (currentWrapLine < maxWrapLines) {
                    // 같은 물리적 라인의 다음 WordWrap 라인으로
                    currentWrapLine++;
                }
                else {
                    // 다음 물리적 라인의 첫 WordWrap 라인으로
                    currentLine++;
                    currentWrapLine = 0;
                }
            }
        }
        else if (totalMoveLines < 0) {
            // 위로 이동
            int absMoveLines = -totalMoveLines;
            for (int i = 0; i < absMoveLines; i++) {
                if (currentLine <= 0 && currentWrapLine <= 0) break;

                if (currentWrapLine > 0) {
                    // 같은 물리적 라인의 이전 WordWrap 라인으로
                    currentWrapLine--;
                }
                else {
                    // 이전 물리적 라인의 마지막 WordWrap 라인으로
                    currentLine--;
                    if (currentLine >= 0) {
                        std::vector<int> wrapPositions = FindWordWrapPosition(currentLine);
                        currentWrapLine = (int)wrapPositions.size();
                    }
                }
            }
        }

        // 경계값 보정
        m_scrollYLine = max(0, min(currentLine, totalLines - 1));
        m_scrollYWrapLine = max(0, currentWrapLine);

        // WordWrap 라인 유효성 검사
        if (m_scrollYLine < totalLines) {
            std::vector<int> wrapPositions = FindWordWrapPosition(m_scrollYLine);
            int maxWrapLines = (int)wrapPositions.size();
            if (m_scrollYWrapLine > maxWrapLines) {
                m_scrollYWrapLine = maxWrapLines;
            }
        }

        // 페이지 이동시 캐럿도 이동 (WordWrap 모드)
        if (moveCaretToo) {
            UpDown(-totalMoveLines);  // 부호 반전: PageUp(음수)→위로, PageDown(양수)→아래로
        }

    }
    else {
        // === 일반 모드 ===
        m_scrollYLine += totalMoveLines;

        // 경계값 보정
        if (m_scrollYLine < 0) {
            m_scrollYLine = 0;
        }
        else {
            int maxScrollLine = m_rope.getSize()-1;
            if (m_scrollYLine > maxScrollLine) {
                m_scrollYLine = maxScrollLine;
            }
        }

        // 페이지 이동시 캐럿도 이동 (일반 모드)
        if (moveCaretToo) {
            m_caretPos.lineIndex += totalMoveLines;

            // 캐럿 경계값 보정
            if (m_caretPos.lineIndex < 0) {
                m_caretPos.lineIndex = 0;
                m_caretPos.column = 0;
            }
            else if (m_caretPos.lineIndex >= (int)m_rope.getSize()) {
                m_caretPos.lineIndex = (int)m_rope.getSize() - 1;
                int lineSize = (int)m_rope.getLineSize(m_caretPos.lineIndex);
                m_caretPos.column = min(m_caretPos.column, lineSize);
            }
            else {
                // 현재 라인에서 컬럼 위치 보정
                int lineSize = (int)m_rope.getLineSize(m_caretPos.lineIndex);
                m_caretPos.column = min(m_caretPos.column, lineSize);
            }
        }
    }
}

// 스크롤바 범위/페이지 크기 재계산
void NemoEdit::RecalcScrollSizes() {
    if (!m_hWnd) return;
	if (m_isUseScrollCtrl && m_showScrollBars == FALSE) return; //스크롤바 표시 X

    CRect client;
    GetClientRect(&client);

    // 스크롤 정보 구조체 초기화
    SCROLLINFO si;
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;

    if (m_wordWrap) {
        // 워드랩 모드에서는 수평 스크롤바를 비활성화
        m_scrollX = 0;

        int visibleLines = max(1, (client.Height()-m_margin.top-m_margin.bottom) / m_lineHeight);

        // 총 라인 수 - 전체 라인 기준으로 사용
        int totalRealLines = max(1, (int)m_rope.getSize());

        // 수직 스크롤바 설정 - 일반 라인 수 기준으로 설정
        si.nMin = 0;
        si.nMax = totalRealLines + visibleLines/2 - 2;
        si.nPage = visibleLines;
        si.nPos = m_scrollYLine;
        NemoSetScrollInfo(SB_VERT, &si, TRUE);
    }
    else {
        // 라인 번호 영역 너비 계산
        int numberAreaWidth = 0;
        if (m_showLineNumbers) {
            numberAreaWidth = CalculateNumberAreaWidth();
        }

        // 일반 텍스트 라인 수 계산
        int totalDisplayLines = max(1,(int)m_rope.getSize());

        // 수직 스크롤바 설정
        si.nMin = 0;
        si.nMax = totalDisplayLines - 1;
        si.nPage = client.Height() / m_lineHeight;  // 한 페이지에 표시되는 라인 수
        si.nPos = m_scrollYLine;
        NemoSetScrollInfo(SB_VERT, &si, TRUE);

        // 수평 스크롤바 설정
        int maxTextWidth = GetMaxWidth(); // 최대 텍스트 폭 계산
        // 라인 번호 영역과 여백 포함
        int totalRequiredWidth = maxTextWidth + numberAreaWidth + m_margin.left + m_imeWidth + m_margin.right;

        si.nMin = 0;
        si.nMax = totalRequiredWidth - 1;  // 전체 필요 너비
        si.nPage = client.Width();         // 화면에 보이는 너비
        si.nPos = m_scrollX;

        // 스크롤 가능한 최대값 제한
        int maxScrollPos = max(0, totalRequiredWidth - client.Width());
        if (m_scrollX > maxScrollPos) {
            m_scrollX = maxScrollPos;
            si.nPos = m_scrollX;
        }

        NemoSetScrollInfo(SB_HORZ, &si, TRUE);
    }
}

// 스크롤바 컨트롤 실행/중지 함수
void NemoEdit::ActiveScrollCtrl(bool isUse) {  
	m_isUseScrollCtrl = isUse;
}

// 스크롤바 표시 설정 함수
void NemoEdit::SetScrollCtrl(bool show) {
	m_showScrollBars = show;
	if (m_showScrollBars == TRUE) {
        ShowScrollBar(SB_VERT, TRUE);
        if(m_wordWrap) ShowScrollBar(SB_HORZ, FALSE);
        else ShowScrollBar(SB_HORZ, TRUE);
	}
	else {
		ShowScrollBar(SB_VERT, FALSE);
		ShowScrollBar(SB_HORZ, FALSE);
	}
}

// 텍스트 전체를 선택하는 함수
void NemoEdit::SelectAll() {
    // 텍스트가 없는 경우 조기 반환
    if (m_rope.empty()) return;

    // 선택 영역을 첫 번째 문자부터 마지막 문자까지 설정
    m_selectInfo.start = TextPos(0, 0);

    // 마지막 라인과 그 길이 계산
    int lastLineIndex = static_cast<int>(m_rope.getSize() - 1);
    int lastLineSize = static_cast<int>(m_rope.getLineSize(lastLineIndex));

    m_selectInfo.end = TextPos(lastLineIndex, lastLineSize);
    m_selectInfo.anchor = m_selectInfo.start;

    // 커서를 선택 영역의 끝으로 이동
    m_caretPos = m_selectInfo.end;

    // 선택 상태 설정
    m_selectInfo.isSelected = true;

    // 뷰 갱신 및 커서 위치 조정
    EnsureCaretVisible();
    UpdateCaretPosition();
    Invalidate(FALSE);
}
// NemoShowScrollBar 래핑 함수
void NemoEdit::NemoShowScrollBar(UINT nBar, BOOL bShow) {
    BOOL bVisibleVert=FALSE;
    BOOL bVisibleHorz = FALSE;
    CScrollBar* pScrollBarVert = GetScrollBarCtrl(SB_VERT);
    CScrollBar* pScrollBarHorz = GetScrollBarCtrl(SB_HORZ);
    if (pScrollBarVert) {  // NULL 체크
        bVisibleVert = pScrollBarVert->IsWindowVisible();
    }
    if (pScrollBarHorz) {  // NULL 체크
        bVisibleHorz = pScrollBarHorz->IsWindowVisible();
    }

	if (m_isUseScrollCtrl == TRUE) {
        if (m_showScrollBars == TRUE) {
			if (nBar == SB_VERT && !bVisibleVert) {
				ShowScrollBar(nBar, TRUE);
			}
			else if (nBar == SB_HORZ && !bVisibleHorz) {
				ShowScrollBar(nBar, TRUE);
			}
        }
        else return;
    }
    else {
        if ( (nBar == SB_VERT && bShow!=bVisibleVert) || (nBar == SB_HORZ && bShow != bVisibleHorz)) {
            SetRedraw(FALSE);  // 화면 갱신 비활성화
            ShowScrollBar(nBar, bShow);
            SetRedraw(TRUE);   // 화면 갱신 활성화
            RedrawWindow();    // 화면 다시 그리기
        }
    }
}

void NemoEdit::NemoSetScrollInfo(UINT nBar, LPSCROLLINFO lpScrollInfo,
    BOOL bRedraw) {
    if (m_isUseScrollCtrl == TRUE) {
        if (m_showScrollBars == TRUE) {
            SetScrollInfo(nBar, lpScrollInfo, bRedraw);
        }
        else return;
    }
    else {
        SetScrollInfo(nBar, lpScrollInfo, bRedraw);
    }
}

void NemoEdit::NemoSetScrollPos(int nBar, int nPos, BOOL bRedraw) {
    if (m_isUseScrollCtrl == TRUE) {
        if (m_showScrollBars == TRUE) {
            SetScrollPos(nBar, nPos, bRedraw);
        }
        else return;
    }
    else {
        SetScrollPos(nBar, nPos, bRedraw);
    }
}

void NemoEdit::DrawLineNo(int lineIndex, int yPos) {
    if (!m_showLineNumbers) return;

    // 라인 번호 그리기
    int numAreaWidth = CalculateNumberAreaWidth();
    CString numStr;
    CSize numSize;
    numStr.Format(_T("%d"), lineIndex + 1);
    numSize = GetTextWidth(numStr.GetString());
    int xPos = numAreaWidth - numSize.cx - 10;
    m_d2Render.DrawLineText(xPos, yPos, nullptr, numStr.GetString(), numStr.GetLength());
}

// 화면 그리기 (더블 버퍼링 사용)
void NemoEdit::OnPaint() {
    CPaintDC dc(this); // WM_PAINT 메시지 처리를 위해 필요

    m_d2Render.BeginDraw();
    CRect client;
    GetClientRect(&client);

    // 배경 지우기
    m_d2Render.Clear(m_colorInfo.textBg);

    // 라인 번호 영역 그리기
    int numberAreaWidth = 0;
    if (m_showLineNumbers) {
        numberAreaWidth = CalculateNumberAreaWidth();
        D2D1_RECT_F lineRect = D2D1::RectF(client.left, client.top, numberAreaWidth, client.bottom);
        m_d2Render.FillSolidRect(lineRect, m_colorInfo.lineNumBg);
    }

    // 텍스트 라인 출력 (선택 영역 강조 포함)
    int y = m_margin.top;

    if (m_wordWrap) {
        // 워드랩 모드에서의 그리기
        int lineIndex = m_scrollYLine;
        int wapLineIndex = m_scrollYWrapLine;

        while (y < client.Height() && lineIndex < (int)m_rope.getSize()) {
            std::wstring lineStr = m_rope.getLine(lineIndex);

            // IME 합성 중인 경우
            bool isImeComposing = m_imeComposition.isComposing && m_imeComposition.lineNo == lineIndex && !m_imeComposition.imeText.empty();
            if (isImeComposing) {
                // 합성 중인 텍스트 출력
                std::wstring preText = lineStr.substr(0, m_caretPos.column);
                preText += m_imeComposition.imeText;
                preText += lineStr.substr(m_caretPos.column);
                lineStr = preText;
            }

            std::vector<int> wrapPositions = FindWordWrapPosition(lineIndex);
            // wordwrap이 없는 경우
            if (wrapPositions.empty()) {
                // 워드랩이 없는 라인 처리
				DrawLineNo(lineIndex, y);
                DrawSegment(lineIndex, 0, lineStr, numberAreaWidth, y);
                y += m_lineHeight;
                lineIndex++;
                continue;
            }

            // 워드랩된 각 세그먼트 그리기
            int startPos = 0; 
            int skipLineCnt = 0;
			if (lineIndex == m_scrollYLine) { // 첫줄일 경우에 시작위치를 topWrapIndex로 설정
                skipLineCnt = m_scrollYWrapLine;
            }
            for (size_t i = 0; i < wrapPositions.size()+1; i++) {
				if (skipLineCnt > 0) {
					skipLineCnt--;
                    if (i>0 && i == wrapPositions.size()) {
                        // 마지막 라인 처리
                        startPos = wrapPositions[i-1];
                    }
                    else startPos = wrapPositions[i];
					continue;
				}
                // 화면 밖으로 나가면 중단
                if (y >= client.Height()) break;

                // 워드랩 라인이 보이는 영역에 있는지 확인
                if (lineIndex >= m_scrollYLine) {
                    int endPos;
					if (i == wrapPositions.size()) {
						// 마지막 라인 처리
						endPos = (int)lineStr.length();
                    }
                    else {
                        endPos = wrapPositions[i];
                    }
                    
					if (i == 0) DrawLineNo(lineIndex, y);
                    std::wstring segment = lineStr.substr(startPos, endPos - startPos);
                    DrawSegment(lineIndex, startPos, segment, numberAreaWidth, y);
                    y += m_lineHeight;
                }

                if (i >= wrapPositions.size()) 
                    break;

				startPos = wrapPositions[i];
            }

            lineIndex++;
        }
    }
    else {
        // 기존 모드 (non-워드랩)
        int lineIndex = m_scrollYLine;
        int maxLine = (int)m_rope.getSize();
        std::wstring lineStr;

        while ( lineIndex< maxLine && y < client.Height()) {
            lineStr = m_rope.getLine(lineIndex);
            // 수평 클리핑 최적화 (화면 밖에 있는 텍스트는 그리지 않음)
            int lineWidth = GetLineWidth(lineIndex);
            if (numberAreaWidth - m_scrollX + lineWidth <= 0) {
                // 완전히 화면 왼쪽 바깥에 있으면 출력하지 않고 다음 라인으로
                DrawLineNo(lineIndex, y);
                y += m_lineHeight;
                lineIndex++;
                continue;
            }

            // IME 합성 중인 경우
            bool isImeComposing = m_imeComposition.isComposing && m_imeComposition.lineNo == lineIndex && !m_imeComposition.imeText.empty();
            if (isImeComposing) {
                // 합성 중인 텍스트 출력
                std::wstring preText = lineStr.substr(0, m_caretPos.column);
                preText += m_imeComposition.imeText;
                preText += lineStr.substr(m_caretPos.column);
                lineStr = preText;
            }

            DrawLineNo(lineIndex, y);
            DrawSegment(lineIndex, 0, lineStr, numberAreaWidth, y);
            y += m_lineHeight;
            lineIndex++;
        }
    }

    // 오프스크린 버퍼를 화면에 출력
    m_d2Render.EndDraw();
}

BOOL NemoEdit::OnEraseBkgnd(CDC* pDC) {
    return TRUE; // 기본 배경 지우기 방지 (더블 버퍼링으로 그릴 것이므로)
}

// 주어진 텍스트를 화면에 출력 (선택 강조 포함)
// lineIndex: 라인 번호
 //segStartIdx: 컬럼 위치 ( wordwrap 이 있을 경우 현재 몇번째 컬럼부터 워드랩 된 것인지 표기 )
// segment: 출력할 텍스트 ( 워드랩인 경우 segStartIdx가 0이 아니면 잘린뒤의 현재 라인에 표시할 텍스트 )
// xOffset: X 좌표
// y: Y 좌표
void NemoEdit::DrawSegment(int lineIndex, size_t segStartIdx, const std::wstring& segment, int xOffset, int y) {
    if (segment.empty()) {
        // 내용이 없는 경우도 캐럿 표시 위해 배경색으로 칠하기
        D2D1_RECT_F lineRect = D2D1::RectF(xOffset - m_scrollX, y, xOffset - m_scrollX + 2, y + m_lineHeight);
        m_d2Render.FillSolidRect(lineRect, m_colorInfo.textBg);
        return;
    }

    std::wstring segText = segment;
    std::wstring tabText = ExpandTabs(segText);
    CRect client;
    GetClientRect(&client);

    // 수직 클리핑 (보이지 않는 라인은 건너뛰기)
    if (y + m_lineHeight <= 0 || y >= client.Height()) {
        return; // 보이지 않는 영역은 출력 생략
    }

    int x = xOffset - m_scrollX + m_margin.left;

    // 텍스트의 전체 너비 계산
    CSize textSize = GetTextWidth(tabText.c_str());

    // 수평 클리핑 (화면 밖에 있는 텍스트는 그리지 않음)
    if (x + textSize.cx <= 0 || x >= client.Width()) {
        return; // 완전히 화면 밖에 있으면 그리지 않음
    }

    // 선택 영역 계산
    bool hasSelection = m_selectInfo.isSelected;
    int selStartCol = 0, selEndCol = 0;

    if (hasSelection) {
        // 선택 영역 정규화
        TextPos s = m_selectInfo.start;
        TextPos e = m_selectInfo.end;

        // 선택 영역이 뒤집혔으면 정렬
        if (e.lineIndex < s.lineIndex || (e.lineIndex == s.lineIndex && e.column < s.column)) {
            s = m_selectInfo.end;
            e = m_selectInfo.start;
        }

        // 텍스트에 선택 영역 표시 계산
        if (lineIndex >= s.lineIndex && lineIndex <= e.lineIndex) {
            // 한라인에만 선택 영역이 존재할 경우
            if (s.lineIndex == e.lineIndex) {
                selStartCol = s.column - segStartIdx;
                if (selStartCol < 0) selStartCol = 0; // 세그먼트 시작보다 작으면 0으로 설정
                selEndCol = e.column - segStartIdx;
                if (selEndCol <= selStartCol) selEndCol = selStartCol; // 선택 영역이 유효하지 않으면 0으로 설정
            }
            // 현재 라인이 선택 영역의 시작인 경우
            else if (lineIndex == s.lineIndex) {
                selStartCol = s.column - segStartIdx;
                if (selStartCol < 0) selStartCol = 0; // 세그먼트 시작보다 작으면 0으로 설정
                selEndCol = segText.size();
            }
            else if (lineIndex == e.lineIndex) {
                selStartCol = 0;
                selEndCol = e.column - segStartIdx;
                if (selEndCol <= selStartCol) selEndCol = selStartCol; // 선택 영역이 유효하지 않으면 0으로 설정
            }
            else {
                selStartCol = 0;
                selEndCol = segText.size();
            }

            // 출력라인에 선택영역이 존재할 경우 탭 확장 적용
            if (selStartCol < selEndCol) {
                // selStartCol 전에 탭 확장만큼 더해주기
                selStartCol += TabCount(segText, selStartCol) * (m_tabSize - 1);
                // selEndCol 전에 탭 확장만큼 더해주기
                selEndCol += TabCount(segText, selEndCol) * (m_tabSize - 1);
            }
        }
    }

    // 클리핑 영역 설정 - 보이는 영역으로 제한
    D2D1_RECT_F clipRect = D2D1::RectF(max(xOffset, x), y, min(client.Width(), x + textSize.cx), y + m_lineHeight);

    if (hasSelection && selEndCol > selStartCol) {
        m_d2Render.DrawEditText(x, y, &clipRect, tabText.c_str(), tabText.size(), true, selStartCol, selEndCol);
    }
    else {
        // 선택 없는 경우 - 클리핑된 영역만 효율적으로 그리기
        m_d2Render.DrawEditText(x, y, &clipRect, tabText.c_str(), tabText.size());
    }
}

// 이전 단어의 시작으로 캐럿 이동
void NemoEdit::MoveCaretToPrevWord() {
	if (m_caretPos.lineIndex > m_rope.getSize()) return;

    const std::wstring& line = m_rope.getLine(m_caretPos.lineIndex);

    // 현재 라인에서 이전 단어 찾기
    if (m_caretPos.column > 0) {
        // 현재 위치가 단어 중간이면 단어 시작으로 이동
        int pos = m_caretPos.column - 1;

        // 공백 건너뛰기
        while (pos >= 0 && iswspace(line[pos])) {
            pos--;
        }

        if (pos >= 0) {
            // 단어 시작 찾기 (영문자, 숫자, 한글, 특수문자 등)
            while (pos >= 0 && !iswspace(line[pos])) {
                pos--;
            }

            m_caretPos.column = pos + 1; // 단어 시작 위치로 이동
        }
        else {
            m_caretPos.column = 0; // 라인 시작으로 이동
        }
    }
    // 현재 라인의 시작에 있다면 이전 라인의 끝으로 이동
    else if (m_caretPos.lineIndex > 0) {
        m_caretPos.lineIndex--;
		std::wstring prevLine = m_rope.getLine(m_caretPos.lineIndex);
        if (!prevLine.empty()) {
            int pos = (int)prevLine.size() - 1;

            // 이전 라인의 마지막 단어 시작 찾기
            // 공백 건너뛰기
            while (pos >= 0 && iswspace(prevLine[pos])) {
                pos--;
            }

            if (pos >= 0) {
                // 단어 시작 찾기
                while (pos >= 0 && !iswspace(prevLine[pos])) {
                    pos--;
                }

                m_caretPos.column = pos + 1; // 단어 시작 위치로 이동
            }
            else {
                m_caretPos.column = 0; // 라인 시작으로 이동
            }
        }
        else {
            m_caretPos.column = 0;
        }
    }
}

// 다음 단어의 시작으로 캐럿 이동
void NemoEdit::MoveCaretToNextWord() {
    if (m_caretPos.lineIndex > m_rope.getSize()) return;

    const std::wstring& line = m_rope.getLine(m_caretPos.lineIndex);
    int lineLength = (int)line.size();

    // 현재 라인에서 다음 단어 찾기
    if (m_caretPos.column < lineLength) {
        int pos = m_caretPos.column;

        // 현재 단어 건너뛰기 (현재 단어 중간에 있을 경우)
        while (pos < lineLength && !iswspace(line[pos])) {
            pos++;
        }

        // 공백 건너뛰기
        while (pos < lineLength && iswspace(line[pos])) {
            pos++;
        }

        if (pos < lineLength) {
            // 다음 단어의 시작점에 도달
            m_caretPos.column = pos;
        }
        else {
            // 라인 끝에 도달했다면 다음 라인 시작으로 이동
            if (m_caretPos.lineIndex < (int)m_rope.getSize() - 1) {
                m_caretPos.lineIndex++;
                m_caretPos.column = 0;
            }
            else {
                m_caretPos.column = lineLength; // 현재 라인의 끝으로 이동
            }
        }
    }
    // 현재 라인의 끝에 있다면 다음 라인의 시작으로 이동
    else if (m_caretPos.lineIndex < (int)m_rope.getSize() - 1) {
        m_caretPos.lineIndex++;
        m_caretPos.column = 0;
    }
}

void NemoEdit::SaveClipBoard(const std::wstring& text) {
	std::wstring clipText;
	if (!OpenClipboard()) return;
    EmptyClipboard();
    SIZE_T size = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (hMem) {
        wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
        if (pMem) {
            wcscpy_s(pMem, text.size() + 1, text.c_str());
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        else {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
}

std::wstring NemoEdit::LoadClipText() {
	if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return L"";
	if (!OpenClipboard()) return L"";
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        CloseClipboard();
        return L"";
    }

    wchar_t* pText = (wchar_t*)GlobalLock(hData);
    if (!pText) {
        CloseClipboard();
        return L"";
    }
    GlobalUnlock(hData);
    CloseClipboard();

	return std::wstring(pText);
}

void NemoEdit::HideIME() {
    // IME 숨기기
    COMPOSITIONFORM cf;
    cf.dwStyle = CFS_CANDIDATEPOS;
    HIMC hIMC = ImmGetContext(GetSafeHwnd());
    ImmSetCompositionWindow(hIMC, &cf);
}

size_t NemoEdit::GetSize() {
    return m_rope.getSize();
}

int NemoEdit::GetCurrentLineNo() {
    return m_caretPos.lineIndex;
}

void NemoEdit::GotoLine(size_t lineNo) {
    m_caretPos.lineIndex = (int)lineNo;
    EnsureCaretVisible();
    Invalidate(FALSE);
}

// 윈도우 크기 조정
void NemoEdit::OnSize(UINT nType, int cx, int cy) {
    CWnd::OnSize(nType, cx, cy);
    if (cx <= 0 || cy <= 0) return;

    // 창 크기가 변경되면 D2Render 크기도 업데이트
    if (cx > 0 && cy > 0) {
        m_d2Render.Resize(cx, cy);
    }

    if (!m_lineHeight) return;

    // wordwrap 모드일 때 너비 재계산
    if (m_wordWrap) {
        CRect client;
        GetClientRect(&client);
        m_wordWrapWidth = client.Width() - CalculateNumberAreaWidth() - m_margin.right - m_margin.left;
    }

    RecalcScrollSizes(); // 스크롤 크기 재계산
    Invalidate(FALSE);
}

// 수직 스크롤 이벤트 처리
void NemoEdit::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) {
    switch (nSBCode) {
        case SB_LINEUP:   ScrollViewBy(0, -1); break;  // 1라인 위로
        case SB_LINEDOWN: ScrollViewBy(0, 1);  break;  // 1라인 아래로
        case SB_PAGEUP:   ScrollViewBy(-1, 0); break;  // 1페이지 위로
        case SB_PAGEDOWN: ScrollViewBy(1, 0);  break;  // 1페이지 아래로
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si = { 0 };
            si.cbSize = sizeof(SCROLLINFO);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(SB_VERT, &si);

            // 직접 위치 설정 (WordWrap 고려)
            if (m_wordWrap) {
                m_scrollYLine = si.nTrackPos;
                m_scrollYWrapLine = 0;  // 썸 드래그시는 라인 시작으로
            }
            else {
                m_scrollYLine = si.nTrackPos;
            }
        }
        break;
        default: break;
    }

    NemoSetScrollPos(SB_VERT, m_scrollYLine, TRUE);
    UpdateCaretPosition(); // 추가: 스크롤 후 캐럿 위치 업데이트
    Invalidate(FALSE);
}

// 수평 스크롤 이벤트 처리
void NemoEdit::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) {
    CRect client;
    GetClientRect(&client);
    int step = 20;
    switch(nSBCode) {
        case SB_LINELEFT: m_scrollX = max(0, m_scrollX - step); break;
        case SB_LINERIGHT: m_scrollX += step; break;
        case SB_PAGELEFT: m_scrollX = max(0, m_scrollX - client.Width()); break;
        case SB_PAGERIGHT: m_scrollX += client.Width(); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si = { 0 };
            si.cbSize = sizeof(SCROLLINFO);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(SB_HORZ, &si); // 다른 방법
			m_scrollX = si.nTrackPos;
        }
        break;
        default: break;
    }
    NemoSetScrollPos(SB_HORZ, m_scrollX, TRUE);
    UpdateCaretPosition(); // 추가: 스크롤 후 캐럿 위치 업데이트
    Invalidate(FALSE);
}

// 마우스 휠 스크롤 처리
BOOL NemoEdit::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	// 아래로 스크롤할 때 마지막 라인이 화면에 보이는지 체크
    if (zDelta < 0) { // 아래로 스크롤할 때만 체크
        CRect client;
        GetClientRect(&client);
        int visibleLines = max(1, (client.Height() - m_margin.top - m_margin.bottom) / m_lineHeight);
        int totalLines = (int)m_rope.getSize();

        if (m_wordWrap) {
            // 워드랩 모드: 현재 화면에 보이는 마지막 라인 계산
            int currentLine = m_scrollYLine;
            int currentWrapLine = m_scrollYWrapLine;
            int screenLinesUsed = 0;

            // 화면에 표시되는 라인들을 세어가면서 마지막 라인 찾기
            while (screenLinesUsed < visibleLines && currentLine < totalLines) {
                std::vector<int> wrapPositions = FindWordWrapPosition(currentLine);
                int wrapLinesInThisLine = (int)wrapPositions.size() + 1; // 워드랩 라인 수 + 1

                if (currentLine == m_scrollYLine) {
                    // 첫 번째 라인은 m_scrollYWrapLine부터 시작
                    int remainingWrapLines = wrapLinesInThisLine - currentWrapLine;
                    if (screenLinesUsed + remainingWrapLines <= visibleLines) {
                        screenLinesUsed += remainingWrapLines;
                        currentLine++;
                        currentWrapLine = 0;
                    }
                    else {
                        screenLinesUsed = visibleLines; // 화면 가득참
                        break;
                    }
                }
                else {
                    if (screenLinesUsed + wrapLinesInThisLine <= visibleLines) {
                        screenLinesUsed += wrapLinesInThisLine;
                        currentLine++;
                    }
                    else {
                        screenLinesUsed = visibleLines; // 화면 가득참
                        break;
                    }
                }
            }

            // 현재 화면에 문서의 마지막 라인이 보이면 스크롤 차단
            if (currentLine >= totalLines) {
                return TRUE;
            }
        }
        else {
            // 비워드랩 모드: 간단한 계산
            int lastVisibleLine = m_scrollYLine + visibleLines - 1;
            if (lastVisibleLine >= totalLines - 1) {
                return TRUE;
            }
        }
    }

    int scrollLines = (zDelta > 0) ? -3 : 3;  // 3라인씩 스크롤
    ScrollViewBy(0, scrollLines);

    NemoSetScrollPos(SB_VERT, m_scrollYLine, TRUE);
    UpdateCaretPosition();
    Invalidate(FALSE);
    return TRUE;
}

void NemoEdit::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    // 클릭 카운트를 2로 설정 (더블 클릭)
    m_clickCount = 2;

    // 마지막 클릭 정보 업데이트
    m_lastClickTime = GetTickCount();
    m_lastClickPos = point;

    // 포커스 설정
    SetFocus();

    // 클릭 위치의 텍스트 좌표 계산
    TextPos pos = GetTextPosFromPoint(point);

    if (pos.lineIndex >= m_rope.getSize()) {
        CWnd::OnLButtonDblClk(nFlags, point);
        return;
    }

    // 현재 라인의 텍스트 가져오기
    std::wstring lineText = m_rope.getLine(pos.lineIndex);

    // 단어 경계 찾기 (단순화된 버전)
    int wordStart, wordEnd;
    FindWordBoundary(lineText, pos.column, wordStart, wordEnd);

    // 선택된 내용이 없으면 중단
    if (wordStart == wordEnd) {
        CWnd::OnLButtonDblClk(nFlags, point);
        return;
    }

    // 선택 영역 설정
    m_selectInfo.start.lineIndex = pos.lineIndex;
    m_selectInfo.start.column = wordStart;
    m_selectInfo.end.lineIndex = pos.lineIndex;
    m_selectInfo.end.column = wordEnd;
    m_selectInfo.isSelected = true;
    m_selectInfo.anchor = m_selectInfo.start;

    // 캐럿은 단어의 끝에 위치
    m_caretPos.lineIndex = pos.lineIndex;
    m_caretPos.column = wordEnd;

    // 화면 갱신
    UpdateCaretPosition();
    Invalidate(FALSE);

    CWnd::OnLButtonDblClk(nFlags, point);
}

// 마우스 왼쪽 버튼 눌렀을 때 (캐럿 이동 및 선택 시작)
void NemoEdit::OnLButtonDown(UINT nFlags, CPoint point) {
    SetFocus();

    // 현재 클릭 시간 가져오기
    DWORD currentClickTime = GetTickCount();

    // 이전 클릭과 시간 간격(500ms) 및 위치(5픽셀) 확인
    bool isDoubleClickTime = (currentClickTime - m_lastClickTime) < 500;
    bool isNearLastClick = abs(point.x - m_lastClickPos.x) < 5 && abs(point.y - m_lastClickPos.y) < 5;

    // 이전 클릭과 같은 위치에서 빠르게 클릭된 경우
    if (isDoubleClickTime && isNearLastClick)
    {
        m_clickCount++;

        // 트리플 클릭 확인 (클릭 카운트가 3)
        if (m_clickCount == 3)
        {
            m_clickCount = 0;  // 카운트 초기화
            HandleTripleClick(point);
            m_lastClickTime = currentClickTime;
            m_lastClickPos = point;
            return;
        }
    }
    else
    {
        // 새로운 클릭 시작
        m_clickCount = 1;
    }

    // 마지막 클릭 정보 저장
    m_lastClickTime = currentClickTime;
    m_lastClickPos = point;

    // 기존 OnLButtonDown 코드 계속 실행
    TextPos pos = GetTextPosFromPoint(point);

    // 캐럿 위치 갱신
    m_caretPos.lineIndex = pos.lineIndex;
    m_caretPos.column = pos.column;

    // Shift가 눌리지 않았으면 새로운 선택 시작
    if(!(nFlags & MK_SHIFT)) {
        m_selectInfo.anchor = m_caretPos;
        m_selectInfo.start = m_selectInfo.end = m_caretPos;
        m_selectInfo.isSelected = false;  // 새 선택 시작 시 초기화
    } else {
        // Shift가 눌린 상태에서 클릭: 기존 선택 확장 또는 새 선택 시작
        // 기존에 선택된 영역이 없으면 anchor가 필요함
        if (!m_selectInfo.isSelected) {
            // 이전에 선택 영역이 없었다면 현재 앵커를 유지
            m_selectInfo.anchor = m_selectInfo.start;
        }

        // 선택 영역 업데이트 (앵커부터 현재 캐럿까지)
        if (m_selectInfo.anchor.lineIndex < m_caretPos.lineIndex ||
            (m_selectInfo.anchor.lineIndex == m_caretPos.lineIndex &&
                m_selectInfo.anchor.column < m_caretPos.column)) {
            m_selectInfo.start = m_selectInfo.anchor;
            m_selectInfo.end = m_caretPos;
        }
        else {
            m_selectInfo.start = m_caretPos;
            m_selectInfo.end = m_selectInfo.anchor;
        }

        // 선택 영역 설정
        m_selectInfo.isSelected = true;
    }
    m_selectInfo.isSelecting = true;
    SetCapture();
    UpdateCaretPosition();
    ShowCaret();
    Invalidate(FALSE);
}

// 마우스 왼쪽 버튼 떼었을 때
void NemoEdit::OnLButtonUp(UINT nFlags, CPoint point) {
    if(m_selectInfo.isSelecting) {
        ReleaseCapture();
        m_selectInfo.isSelecting = false;
    }
    // 텍스트 드래그 앤 드롭 구현 시 여기서 drop 처리 가능 (생략)
}

// 마우스 이동 (드래그에 의한 선택 영역 확대)
void NemoEdit::OnMouseMove(UINT nFlags, CPoint point) {
    if (m_selectInfo.isSelecting && (nFlags & MK_LBUTTON)) {
        // 영역 밖으로 드래그 시 자동 스크롤
        CRect client;
        GetClientRect(&client);
        if (point.y < 0) {
            OnVScroll(SB_LINEUP, 0, NULL);
        }
        else if (point.y > client.bottom) {
            OnVScroll(SB_LINEDOWN, 0, NULL);
        }

        // 워드랩 모드와 일반 모드 분리
        if (m_wordWrap) {
            // 워드랩 모드에서의 마우스 위치 계산
            TextPos newPos = GetTextPosFromPoint(point);

            if (newPos.lineIndex != m_caretPos.lineIndex || newPos.column != m_caretPos.column) {
                m_caretPos = newPos;

                // 선택 영역 업데이트
                if (m_selectInfo.anchor.lineIndex < m_caretPos.lineIndex ||
                    (m_selectInfo.anchor.lineIndex == m_caretPos.lineIndex &&
                        m_selectInfo.anchor.column < m_caretPos.column)) {
                    m_selectInfo.start = m_selectInfo.anchor;
                    m_selectInfo.end = m_caretPos;
                }
                else {
                    m_selectInfo.start = m_caretPos;
                    m_selectInfo.end = m_selectInfo.anchor;
                }

                // 앵커와 캐럿 위치가 다르면 선택 영역 있음
                m_selectInfo.isSelected = !(m_selectInfo.anchor.lineIndex == m_caretPos.lineIndex &&
                    m_selectInfo.anchor.column == m_caretPos.column);

                UpdateCaretPosition();
                Invalidate(FALSE);
            }
        }
        else {
            // 기존 일반 모드 코드를 GetTextPosFromPoint 사용으로 대체
            TextPos newPos = GetTextPosFromPoint(point);

            // 캐럿 위치가 변경된 경우에만 업데이트
            if (newPos.lineIndex != m_caretPos.lineIndex || newPos.column != m_caretPos.column) {
                m_caretPos = newPos;

                // 선택 영역 업데이트
                if (m_selectInfo.anchor.lineIndex < m_caretPos.lineIndex ||
                    (m_selectInfo.anchor.lineIndex == m_caretPos.lineIndex &&
                        m_selectInfo.anchor.column < m_caretPos.column)) {
                    m_selectInfo.start = m_selectInfo.anchor;
                    m_selectInfo.end = m_caretPos;
                }
                else {
                    m_selectInfo.start = m_caretPos;
                    m_selectInfo.end = m_selectInfo.anchor;
                }

                // 앵커와 캐럿 위치가 다르면 선택 영역 있음
                if (m_selectInfo.anchor.lineIndex != m_caretPos.lineIndex ||
                    m_selectInfo.anchor.column != m_caretPos.column) {
                    m_selectInfo.isSelected = true;
                }

                UpdateCaretPosition();
                Invalidate(FALSE);
            }
        }
    }
}

// 키 입력 (문자)
void NemoEdit::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags) {
    if(nChar == 8 || nChar == 127 || m_isReadOnly) {
        // Backspace(8)나 Delete(127)는 OnKeyDown에서 처리
        return;
    }
    
    if(nChar == '\r' || nChar == '\n' || nChar >= 32) {
        // 일반 문자 입력
        if (m_selectInfo.isSelected) {
            std::wstring tmpStr(1, nChar);
            ReplaceSelection(tmpStr);
        } else InsertChar((wchar_t)nChar);
    }
    EnsureCaretVisible();
    Invalidate(FALSE); // 배경 그리지 않음
}

// 키 입력 (특수키 등)
void NemoEdit::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
    if (nChar == VK_SHIFT || nChar == VK_CONTROL) {
        return;
    }

    if (m_isReadOnly) {
        switch (nChar) {
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_ESCAPE:
            break; // 네비게이션 키 허용
        default:
            return; // 다른 키는 무시
        }
    }

    // ESC 키 처리 - 선택 영역이 있으면 취소
    if (nChar == VK_ESCAPE) {
        if (m_selectInfo.isSelected) {
            CancelSelection();
            EnsureCaretVisible();
            Invalidate(FALSE);
            return;
        }
    }

    TextPos oldCaret = m_caretPos;
    bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    int lineSize = 0;
    switch(nChar) {
    case VK_LEFT:
        if (!shift&& m_selectInfo.isSelected) {
            CancelSelection();
        }
        if (ctrl) {
            // Ctrl+왼쪽 화살표: 이전 단어의 시작으로 이동
            MoveCaretToPrevWord();
        }
        else if(m_caretPos.column > 0) {
            m_caretPos.column--;
        } else if(m_caretPos.lineIndex > 0) {
            m_caretPos.lineIndex--;
            m_caretPos.column = (int)m_rope.getLineSize(m_caretPos.lineIndex);
        }
        break;
    case VK_RIGHT:
        {
            if (!shift && m_selectInfo.isSelected) {
                CancelSelection();
            }
            if (ctrl) {
                // Ctrl+오른쪽 화살표: 다음 단어의 시작으로 이동
                MoveCaretToNextWord();
            }
            else {
                if (m_caretPos.lineIndex<m_rope.getSize()) {
					lineSize = (int)m_rope.getLineSize(m_caretPos.lineIndex);
                    if (m_caretPos.column < lineSize) {
                        m_caretPos.column++;
                    }
                    else if (m_caretPos.lineIndex < m_rope.getSize() - 1) {
                        m_caretPos.lineIndex++;
                        m_caretPos.column = 0;
                    }
                }
            }
        }
        break;
    case VK_UP:
        if (!shift && m_selectInfo.isSelected) {
            CancelSelection();
        }
        UpDown(CURSOR_UP);
        break;
    case VK_DOWN:
        if (!shift&& m_selectInfo.isSelected) {
            CancelSelection();
        }
        UpDown(CURSOR_DOWN);
        break;
    case VK_HOME:
        if (!shift&& m_selectInfo.isSelected) {
            CancelSelection();
        }
        if(ctrl) {
            m_caretPos.lineIndex = 0;
            m_caretPos.column = 0;
        } else {
            m_caretPos.column = 0;
        }
        break;
    case VK_END:
        if (!shift && m_selectInfo.isSelected) {
            CancelSelection();
        }
        if (ctrl) {
            m_caretPos.lineIndex = (int)m_rope.getSize() - 1;
        }
		lineSize = (int)m_rope.getLineSize(m_caretPos.lineIndex);
        m_caretPos.column = m_caretPos.column< lineSize ? lineSize : 0;
        break;
    case VK_PRIOR: { // Page Up
        if (!shift && m_selectInfo.isSelected) {
            CancelSelection();
        }
        ScrollViewBy(-1, 0);  // 1페이지 위로
        break;
    }
    case VK_NEXT: { // Page Down
        if (!shift && m_selectInfo.isSelected) {
            CancelSelection();
        }
        ScrollViewBy(1, 0);   // 1페이지 아래로
        break;
    }
    case VK_DELETE:
        if(ctrl) break;  // Ctrl+Del 처리 없음
        if(m_selectInfo.isSelected) {
            DeleteSelection();
        } else {
            DeleteChar(false);
        }
        break;
    case VK_BACK:
        if(ctrl) break;  // Ctrl+Backspace 처리 없음
        if(m_selectInfo.isSelected) {
            DeleteSelection();
        } else {
            DeleteChar(true);
        }
        break;
    case VK_TAB:
        if (m_isReadOnly) return;

        // 여러 줄 선택 시 특별 처리
        if (m_selectInfo.isSelected) {
            if (m_selectInfo.start.lineIndex != m_selectInfo.end.lineIndex) {
                // Shift+Tab 처리는 추후 구현 가능
                if (shift) {
                    RemoveTabFromSelectedLines(); // 탭 제거 기능 구현 시 주석 해제
                    return;
                }
                else {
                    AddTabToSelectedLines();
                }
                return; // 이벤트 처리 완료
            }
			else {
                std::wstring tmpStr(1, L'\t');
            	ReplaceSelection(tmpStr);
			}
        }
        else {
            // 일반 탭 처리 (기존 코드)
            InsertChar(L'\t');
        }
        EnsureCaretVisible();
        Invalidate(FALSE);
        break;
    default:
        break;
    }

    // Shift 눌린 경우 선택 영역 확장
    if (shift) {
        if(!m_selectInfo.isSelected) {
            // 기존에 선택이 없었다면 앵커 설정
            m_selectInfo.anchor = oldCaret;
        }
        if(m_selectInfo.anchor.lineIndex < m_caretPos.lineIndex || 
           (m_selectInfo.anchor.lineIndex == m_caretPos.lineIndex && m_selectInfo.anchor.column < m_caretPos.column)) {
            m_selectInfo.start = m_selectInfo.anchor;
            m_selectInfo.end = m_caretPos;
        } else {
            m_selectInfo.start = m_caretPos;
            m_selectInfo.end = m_selectInfo.anchor;
        }

        // 앵커와 캐럿 위치가 다르면 선택 영역이 있는 것으로 간주
        m_selectInfo.isSelected = !(m_selectInfo.start.lineIndex == m_selectInfo.end.lineIndex &&
            m_selectInfo.start.column == m_selectInfo.end.column);
    }

    EnsureCaretVisible();
    Invalidate(FALSE);
}

// 포커스 받았을 때 (캐럿 생성 및 표시)
void NemoEdit::OnSetFocus(CWnd* pOldWnd) {
    CWnd::OnSetFocus(pOldWnd);
    CreateSolidCaret(2, m_lineHeight-m_lineSpacing); 
    UpdateCaretPosition();
    ShowCaret();
    HideIME();
}

// 포커스 잃었을 때 (캐럿 숨김)
void NemoEdit::OnKillFocus(CWnd* pNewWnd) {
    CWnd::OnKillFocus(pNewWnd);
    HideCaret();
    DestroyCaret();
}


// Ctrl+key 처리
BOOL NemoEdit::PreTranslateMessage(MSG* pMsg)
{
	// Ctrl 조합키 KeyDown 처리 : 여기에서 처리하는 이유는 Ctrl+key가 SystemKey처리때문에 KeyDown에 입력되지 않기때문
    if (pMsg->message == WM_KEYDOWN)
    {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        if (ctrl) {
            UINT nChar = (UINT)pMsg->wParam;
            if (m_isReadOnly && nChar != 'C') {
                return CWnd::PreTranslateMessage(pMsg);
            }
            bool isAct = true;
            switch (nChar) {
                case 'C': Copy(); break;
                case 'X': Cut(); break;
                case 'V': Paste(); break;
                case 'A': {
                    // 모두 선택
                    int lastLineIndex = (int)m_rope.getSize() - 1;
                    int lastLineSize = (int)m_rope.getLineSize(lastLineIndex);
                    m_selectInfo.start = TextPos(0, 0);
                    m_selectInfo.end = TextPos(lastLineIndex, lastLineSize);
                    m_selectInfo.anchor = m_selectInfo.start;
                    m_caretPos = m_selectInfo.end;
                    m_selectInfo.isSelected = true;
                }
                break;
                case 'Z': Undo(); break;
                case 'Y': Redo(); break;
                default: isAct = false; break;
            }
            if (isAct) {
                EnsureCaretVisible();
                Invalidate(FALSE);
                return TRUE;
            }
        }
    }

    return CWnd::PreTranslateMessage(pMsg);
}

// 조합 시작 시 호출
LRESULT NemoEdit::OnImeStartComposition(WPARAM wParam, LPARAM lParam)
{
    m_imeComposition.isComposing = true;         // 조합 시작 플래그 설정
    m_imeComposition.imeText.clear();    // 조합 중인 문자열 초기화
    m_imeComposition.lineNo = m_caretPos.lineIndex;  // 현재 커서 위치 저장
    m_imeComposition.startPos = m_caretPos.column;

    if (!(m_selectInfo.start.lineIndex == m_selectInfo.end.lineIndex && m_selectInfo.start.column == m_selectInfo.end.column)) {
        DeleteSelection();
    }

    return DefWindowProc(WM_IME_STARTCOMPOSITION, wParam, lParam);
}

// 조합 중간에 호출
LRESULT NemoEdit::OnImeComposition(WPARAM wParam, LPARAM lParam)
{
    if (m_imeComposition.isComposing)
    {
        HIMC hIMC = ImmGetContext(GetSafeHwnd());
        if (hIMC)
        {
            // 조합 중인 문자열 가져오기
            DWORD dwSize = ImmGetCompositionString(hIMC, GCS_COMPSTR, NULL, 0);
            if (dwSize > 0)
            {
                std::vector<wchar_t> buffer(dwSize / sizeof(wchar_t) + 1);
                ImmGetCompositionString(hIMC, GCS_COMPSTR, buffer.data(), dwSize);

                m_imeComposition.imeText = buffer.data(); // 조합 중인 문자열 저장
                m_imeComposition.lineNo = m_caretPos.lineIndex;  // 현재 커서 위치 저장
                m_imeComposition.startPos = m_caretPos.column;
            }
            ImmReleaseContext(GetSafeHwnd(), hIMC);
        }
    }

    return DefWindowProc(WM_IME_COMPOSITION, wParam, lParam);
}

// 조합 완료 후 최종 문자 입력 시 호출
LRESULT NemoEdit::OnImeChar(WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(WM_IME_CHAR, wParam, lParam);
}

LRESULT NemoEdit::OnImeEndComposition(WPARAM wParam, LPARAM lParam)
{
    // 이 함수는 IME 조합이 종료되었을 때 호출됩니다.

    m_imeComposition.isComposing = false;
    m_imeComposition.imeText.clear();

    // Windows가 이 메시지를 처리하도록 하려면
    return DefWindowProc(WM_IME_ENDCOMPOSITION, wParam, lParam);
}

Rope::Rope() : root(nullptr), m_balanceCnt(0) {
    root = new RopeNode();
}

Rope::~Rope() {}

void Rope::insert(size_t lineIndex, const std::wstring& text) {
    size_t offset;
    RopeNode* leaf = findLeaf(root, lineIndex, offset);
    if (leaf && leaf->length >= offset) {
        auto it = getIterator(lineIndex);
        auto newIt = lines.insert(it, text);

        leaf->data.insert(leaf->data.begin() + offset, newIt);
        leaf->length++;
        updateLengthUpward(leaf, 1);

        // 노드 크기 기준 분할
        if (leaf->length > SPLIT_THRESHOLD) {
            splitNode(leaf);
        }
        
        // 주기적인 전체 트리 균형 체크 (삽입 카운터 사용)
        if (++m_balanceCnt % (SPLIT_THRESHOLD * 2) == 0) {
            m_balanceCnt = 0;
            if (isUnbalanced()) {
                balanceRope();
            }
        }
    }
}

void Rope::insertAt(size_t lineIndex, size_t offset, const std::wstring& text) {
    if (lineIndex > lines.size()) return;

    auto it = getIterator(lineIndex);
    if (it == lines.end()) {
        insert(lineIndex, text);
    }
    else {
        size_t startPos = offset;
        if (it->empty() || offset >= it->size()) {
            startPos = it->size();
        }
        it->insert(startPos, text);
    }
    return;
}

void Rope::insertBack(const std::wstring& text) {
    size_t lineIndex = lines.size();
    insert(lineIndex, text);
}

void Rope::erase(size_t lineIndex) {
    size_t offset;
    RopeNode* leaf = findLeaf(root, lineIndex, offset);
    if (!leaf || leaf->length < offset) return;

    lines.erase(leaf->data[offset]);
    leaf->data.erase(leaf->data.begin() + offset);
    leaf->length--;
    updateLengthUpward(leaf, -1);

    if (leaf->length < MERGE_THRESHOLD/2) {
        mergeIfNeeded(leaf);
    }

    if (++m_balanceCnt % (MERGE_THRESHOLD * 2) == 0) {
		m_balanceCnt = 0;
        if (isUnbalanced()) {
            balanceRope();
        }
    }
}

void Rope::eraseAt(size_t lineIndex, size_t offset, size_t size) {
    if (lineIndex >= lines.size()) return;

    size_t actualSize = size;
    auto it = getIterator(lineIndex);
    if (it->empty() || offset >= it->size()) return;
    if (offset + size > it->size()) actualSize = it->size() - offset;
    if (actualSize > 0) it->erase(offset, actualSize);
}

void Rope::update(size_t lineIndex, const std::wstring& newText) {
    if (lineIndex >= lines.size()) return;

    auto it = getIterator(lineIndex);
    *it = newText;
}

void Rope::mergeLine(size_t lineIndex)
{
    if (lineIndex + 1 >= getSize())
        return;

    std::wstring strLine = getLine(lineIndex);
    std::wstring strNextLine = getLine(lineIndex + 1);
    strLine += strNextLine;
    update(lineIndex, strLine);
    erase(lineIndex + 1);
}

bool Rope::clear() {
    deleteAllNodes(root);
    lines.clear();
    root = new RopeNode();
    return true;
}

bool Rope::empty() {
    return lines.empty();
}

std::list<std::wstring>::iterator Rope::getIterator(size_t lineIndex) {
    size_t offset;
    RopeNode* leaf = findLeaf(root, lineIndex, offset);
    if (!leaf || offset >= leaf->data.size()) return lines.end();
    return leaf->data[offset];
}

std::list<std::wstring>::iterator Rope::getEnd() {
    return lines.end();
}

std::list<std::wstring>::iterator Rope::getBegin() {
    return lines.begin();
}

size_t Rope::getSize() {
    return lines.size();
}

size_t Rope::getLineSize(size_t lineIndex) {
    auto it = getIterator(lineIndex);
    return it == lines.end() ? 0 : (*it).size();
}

std::wstring Rope::getLine(size_t lineIndex) {
    auto it = getIterator(lineIndex);
    if (it == lines.end()) return L"";
    return *it;
}

std::wstring Rope::getText() {
    std::wstring text = L"";
    int lineCnt = 0;
    for (const auto& line : lines) {
        if (lineCnt++ > 0) text += L"\r\n";
        text += line;
    }
    return text;
}

// LineIndex도 Column도 0부터 시작
std::wstring Rope::getTextRange(size_t startLineIndex, size_t startLineColumn, size_t endLineIndex, size_t endLineColumn) {
    std::wstring text = L"";
    int lineCnt = 0;
    auto itStart = getIterator(startLineIndex);
    auto itEnd = getIterator(endLineIndex);
    if (itStart == lines.end() || itEnd == lines.end())
        return text;

    if (startLineIndex == endLineIndex) {
        // 한 라인 내에서 선택
        text = itStart->substr(startLineColumn, endLineColumn - startLineColumn);
    }
    else {
        // 여러 라인에 걸쳐 선택
        text += itStart->substr(startLineColumn);
        text += L"\r\n";
        if (endLineIndex - startLineIndex > 1) {
            auto it = std::next(itStart);
            for (size_t line = startLineIndex + 1; line < endLineIndex; line++) {
                if (it == lines.end()) break;
                text += *it;
                text += L"\r\n";
                ++it;
            }
        }
        text += itEnd->substr(0, endLineColumn);
    }
    return text;
}

// Implementation of methods
RopeNode* Rope::findLeaf(RopeNode* node, size_t idx, size_t& offset) {
    if (!node) return nullptr;

    if (node->isLeaf) {
        offset = idx;
        return node;
    }

    // idx는 0부터 length는 1부터 시작
    if (idx < node->length)
        return findLeaf(node->left, idx, offset);
    else
        return findLeaf(node->right, idx - node->length, offset);
}

void Rope::updateLengthUpward(RopeNode* node, int addCnt) {
    while (node) {
        if (node->parent) {
            // parent의 left일 경우
            if (node->parent->left == node) {
                node->parent->length += addCnt;
            }

            node = node->parent;
        }
        else break;
    }
}

void Rope::splitNode(RopeNode* leaf) {
    if (!leaf->isLeaf || leaf->length <= SPLIT_THRESHOLD) return;

    // 분할 노드 생성
    RopeNode* newLeaf = new RopeNode();
    newLeaf->isLeaf = true;

    // 기존 노드에서 중간 이터레이터 지정
    auto midIter = leaf->data.begin() + leaf->data.size() / 2;
    // 분할 노드에 데이터 복사
    newLeaf->data.assign(midIter, leaf->data.end());
    // 기존 노드 절반 삭제
    leaf->data.resize(leaf->data.size() / 2);

    // length 업데이트
    leaf->length = leaf->data.size();
    newLeaf->length = newLeaf->data.size();

    // 내부 노드 추가 : leaf의 위치 정보로 셋팅
    RopeNode* newInternal = new RopeNode();
    newInternal->isLeaf = false;
    newInternal->left = leaf; // 기존 노드는 왼쪽
    newInternal->right = newLeaf; // 새로운 노드는 오른쪽
    newInternal->length = leaf->length; // 왼쪽 노드의 길이만 저장
    newInternal->parent = leaf->parent;

    // leaf가 root가 아니면 내부 노드와 연결
    if (newInternal->parent == nullptr) {
        root = newInternal;
    }
    else {
        // leaf가 왼쪽 자식이면 왼쪽에 연결
        if (newInternal->parent->left == leaf)
            newInternal->parent->left = newInternal;
        else
            newInternal->parent->right = newInternal;
    }

    leaf->parent = newLeaf->parent = newInternal;
}

bool Rope::splitNodeByExact(RopeNode* leaf, size_t cutSize) {
    // 유효성 검사: 리프 노드가 아니거나 크기가 0이거나 cutSize가 노드 크기보다 크면 실패
    if (!leaf || !leaf->isLeaf || leaf->data.size() < 2 || cutSize >= leaf->data.size()) {
        return false;
    }

    try {
        // 분할 노드 생성
        RopeNode* newLeaf = new RopeNode();
        newLeaf->isLeaf = true;

        // cutSize 위치의 이터레이터 찾기
        auto cutIter = leaf->data.begin() + cutSize;

        // 분할 노드에 데이터 복사 (cutIter부터 끝까지)
        newLeaf->data.assign(cutIter, leaf->data.end());

        // 기존 노드의 크기를 cutSize로 조정 (cutIter 이후 삭제)
        leaf->data.erase(cutIter, leaf->data.end());

        // length 업데이트
        leaf->length = leaf->data.size();
        newLeaf->length = newLeaf->data.size();

        // 내부 노드 추가: leaf의 위치 정보로 설정
        RopeNode* newInternal = new RopeNode();
        newInternal->isLeaf = false;
        newInternal->left = leaf;       // 기존 노드는 왼쪽
        newInternal->right = newLeaf;   // 새로운 노드는 오른쪽
        newInternal->length = leaf->length; // 왼쪽 노드의 길이만 저장
        newInternal->parent = leaf->parent;

        // leaf가 root가 아니면 내부 노드와 연결
        if (newInternal->parent == nullptr) {
            root = newInternal;
        }
        else {
            // leaf가 왼쪽 자식이면 왼쪽에 연결
            if (newInternal->parent->left == leaf)
                newInternal->parent->left = newInternal;
            else
                newInternal->parent->right = newInternal;
        }

        leaf->parent = newLeaf->parent = newInternal;
        return true; // 성공
    }
    catch (...) {
        // 예외 발생 시 실패 반환
        return false;
    }
}

void Rope::mergeIfNeeded(RopeNode* leaf) {
    if (!leaf->parent) return;

    RopeNode* sibling = leaf->parent->left == leaf ? leaf->parent->right : leaf->parent->left;
    if (sibling && sibling->isLeaf && leaf->data.size() + sibling->data.size() <= MERGE_THRESHOLD) {
        leaf->data.insert(leaf->data.end(), sibling->data.begin(), sibling->data.end());
        leaf->length = leaf->data.size();

        RopeNode* parent = leaf->parent;
        leaf->parent = parent->parent;

        if (parent->parent) {
            if (parent->parent->left == parent) parent->parent->left = leaf;
            else parent->parent->right = leaf;
        }
        else {
            root = leaf;
        }

        sibling->left = sibling->right = nullptr;
        parent->left = parent->right = nullptr;
        delete sibling;
        delete parent;
    }
}

void Rope::deleteLeafNode(RopeNode* node) {
    if (!node || !node->isLeaf) return;

    RopeNode* parent = node->parent;

    // m_lines에서 data에 해당하는 부분 삭제
    if (!node->data.empty()) {
        auto it = node->data.begin();
        auto itEnd = node->data.begin() + node->data.size() - 1;
        int eraseCnt = (int)node->data.size();
        if (*it != *itEnd) lines.erase(*it, *itEnd);
        lines.erase(*itEnd);

        // 상위 length 업데이트
        updateLengthUpward(node, -eraseCnt);
    }

    // 부모가 존재하면 현재 노드를 부모에서 제거
    if (parent) {
        if (parent->left == node) parent->left = nullptr;
        else if (parent->right == node) parent->right = nullptr;
    }

    delete node;
}

void Rope::deleteAllNodes(RopeNode* node) {
    // 스택 기반 반복적 삭제
    std::stack<RopeNode*> nodeStack;
    if (node) nodeStack.push(node);

    while (!nodeStack.empty()) {
        RopeNode* current = nodeStack.top();
        nodeStack.pop();

        // 자식 노드들을 스택에 추가
        if (current->right) nodeStack.push(current->right);
        if (current->left) nodeStack.push(current->left);

        // 현재 노드 삭제
        delete current;
    }
}

// 트리 불균형 체크 함수 : 최소 깊이와 최대 깊이가 2배이면 불균형
bool Rope::isUnbalanced() {
    if (!root || root->isLeaf) return false;

    int maxDepth = getMaxDepth(root);
    int minDepth = getMinDepth(root);

    // 최대 깊이가 최소 깊이의 2배 이상이면 불균형 상태로 간주
    return (maxDepth > minDepth * 2);
}

// 최대 깊이 계산
int Rope::getMaxDepth(RopeNode* node) {
    if (!node) return 0;
    if (node->isLeaf) return 1;

    int leftDepth = getMaxDepth(node->left);
    int rightDepth = getMaxDepth(node->right);

    return 1 + max(leftDepth, rightDepth);
}

// 최소 깊이 계산
int Rope::getMinDepth(RopeNode* node) {
    if (!node) return 0;
    if (node->isLeaf) return 1;

    // 자식 노드가 없는 경우 처리
    if (!node->left) return 1 + getMinDepth(node->right);
    if (!node->right) return 1 + getMinDepth(node->left);

    int leftDepth = getMinDepth(node->left);
    int rightDepth = getMinDepth(node->right);

    return 1 + min(leftDepth, rightDepth);
}


void Rope::eraseRange(size_t startLine, size_t eraseSize) {
    if (eraseSize == 0) return; // 유효하지 않은 범위 방지

    size_t startOffset, endOffset;
    RopeNode* startNode = findLeaf(root, startLine, startOffset);
    RopeNode* endNode = findLeaf(root, startLine + eraseSize - 1, endOffset);

    if (!startNode || !endNode) return; // 노드가 존재하지 않는 경우

    // 1. startNode에서 startOffset 이후의 데이터를 제거
    if (startNode == endNode) {
        // 같은 노드에 startLine과 endLine이 있는 경우
        auto it = startNode->data.begin() + startOffset;
        auto itEnd = startNode->data.begin() + endOffset;
        if (*it != *itEnd) lines.erase(*it, *itEnd); // 실제 텍스트 영역 삭제
        lines.erase(*itEnd); // 마지막 삭제
        startNode->data.erase(it, itEnd + 1); // 노드에서 이터레이터 삭제
        startNode->length = startNode->data.size();
        updateLengthUpward(startNode, -(int)eraseSize);
    }
    else {
        // 2. startNode의 startOffset 이후의 데이터 삭제
        auto it = startNode->data.begin() + startOffset;
        auto itEnd = startNode->data.begin() + startNode->data.size() - 1;
        if (*it != *itEnd) lines.erase(*it, *itEnd); // 실제 텍스트 영역 삭제
        lines.erase(*itEnd); // 마지막 삭제
        startNode->data.erase(it, startNode->data.end());
        size_t removedCount = startNode->length - startOffset;
        startNode->length = startNode->data.size();
        updateLengthUpward(startNode, -(int)removedCount);

        // 3. endNode의 앞부분 데이터 삭제
        it = endNode->data.begin();
        itEnd = endNode->data.begin() + endOffset;
        if (*it != *itEnd) lines.erase(*it, *itEnd); // 실제 텍스트 영역 삭제
        lines.erase(*itEnd); // 마지막 삭제
        endNode->data.erase(it, itEnd + 1);
        removedCount = endOffset;
        endNode->length = endNode->data.size();
        updateLengthUpward(endNode, -(int)removedCount);

        // 4. startNode와 endNode 사이의 노드들을 제거
        size_t nextOffset = 0;
        while (startNode) {
            size_t nextIndex = startLine + startNode->length - startOffset; // 다음 노드의 시작점
            RopeNode* nextNode = findLeaf(root, nextIndex, nextOffset);
            if (!nextNode || nextNode == endNode) break;

            deleteLeafNode(nextNode); // 중간 노드 삭제
        }

        // 5. 트리 재구성
        balanceRope();
    }
}

// 휴리스틱 최적화
void Rope::balanceRope() {
    if (!root || root->isLeaf) return;

    // 리프 노드를 수집
    std::list<RopeNode*> leaves;
    collectLeafNodes(root, leaves);
    if (leaves.empty()) {
        std::cerr << "오류: collectLeafNodes가 리프 노드를 수집하지 못함!" << std::endl;
        exit(1);
    }

    // 내부 노드를 수집
    std::vector<RopeNode*> internals;
    collectInternalNodes(root, internals);

    for (auto node : internals) {
        if (node == root) continue;
        node->left = node->right = nullptr;
        delete node;
    }

    // 새로운 균형 잡힌 트리 구축
    RopeNode* newRoot = buildBalancedTree(leaves, 0, (int)leaves.size() - 1);
    if (!newRoot) {
        std::cerr << "오류: buildBalancedTree가 새로운 트리를 생성하지 못함!" << std::endl;
        exit(1);
    }

    // 기존 root를 삭제 : leaf 노드라면 newRoot에 포함되어있음.
    if (!root->isLeaf) {
        delete root;
        root = nullptr;
    }

    // 기존 root를 새로운 루트로 교체
    root = newRoot;

    updateNodeLengths(root);
}

void Rope::insertMultiple(size_t lineIndex, std::list<std::wstring>& newLines) {
    if (!root) return;
    bool isEnd = false;
    size_t insertIndex = lineIndex;
    if (lineIndex >= lines.size()) {
        isEnd = true;
        insertIndex = lines.size(); // lines.size()와 같으면 end()라고 보자.
    }

    // 삽입 지점의 리프 노드를 삽입할 부분의 좌우로 분할 : end()이면 back에 붙여넣기.
    size_t offset;
    RopeNode* divLeaf = findLeaf(root, insertIndex, offset);
    if (!divLeaf || isEnd) isEnd = true;
    else if (offset > 0 && offset < divLeaf->data.size()) {
        splitNodeByExact(divLeaf, offset);
    }
    divLeaf = findLeaf(root, insertIndex, offset); // 분할되었으면 다시 지정
    auto insertPos = getIterator(insertIndex);
    // 리프 노드를 수집
    std::list<RopeNode*> leaves;
    collectLeafNodes(root, leaves);
    if (leaves.empty()) {
        std::cerr << "오류: collectLeafNodes가 리프 노드를 수집하지 못함!" << std::endl;
        exit(1);
    }
    // 내부 노드를 수집 및 삭제
    std::vector<RopeNode*> internals;
    collectInternalNodes(root, internals);

    for (auto node : internals) {
        if (node == root) continue;
        delete node;
    }

    // 데이터 만들기 : lines, leafNode
    std::list<RopeNode*> newLeafNodes;

    RopeNode* addLeaf = new RopeNode();
    addLeaf->isLeaf = true;
    addLeaf->length = 0;
    addLeaf->left = addLeaf->right = addLeaf->parent = nullptr;

    for (auto it = newLines.begin(); it != newLines.end(); ++it) {
        // 현재 리프 노드에 라인 추가
        addLeaf->data.push_back(it);
        addLeaf->length++;

        // 현재 리프 노드가 가득 찼는지 확인
        if (addLeaf->length >= SPLIT_THRESHOLD) {
            newLeafNodes.push_back(addLeaf);
            addLeaf = new RopeNode();
            addLeaf->isLeaf = true;
            addLeaf->length = 0;
            addLeaf->left = addLeaf->right = addLeaf->parent = nullptr;
        }
    }

    // 마지막 리프 노드 처리
    if (addLeaf->length > 0) {
        newLeafNodes.push_back(addLeaf);
    }
    else {
        delete addLeaf;
    }

    // 데이터 붙이기
    if (isEnd) {
        lines.splice(lines.end(), newLines);
        leaves.splice(leaves.end(), newLeafNodes);
    }
    else {
        if (insertPos != lines.end()) lines.splice(insertPos, newLines);
        else lines.splice(lines.end(), newLines);

        bool isFound = false;
        if (!divLeaf) {
            leaves.splice(leaves.end(), newLeafNodes);
        }
        else {
            auto insertIt = leaves.end();
            for (auto it = leaves.begin(); it != leaves.end(); ++it) {
                if (*it == divLeaf) {
                    insertIt = it;
                    isFound = true;
                    break;
                }
            }
            if (isFound) {
                leaves.splice(insertIt, newLeafNodes);
            }
            else {
                leaves.splice(leaves.end(), newLeafNodes);
            }
        }
    }
    // 새로운 균형 잡힌 트리 구축
    RopeNode* newRoot = buildBalancedTree(leaves, 0, (int)leaves.size() - 1);
    if (!newRoot) {
        std::cerr << "오류: buildBalancedTree가 새로운 트리를 생성하지 못함!" << std::endl;
        exit(1);
    }
    // 기존 root를 삭제 : leaf 노드라면 newRoot에 포함되어있음.
    if (!root->isLeaf) {
        delete root;
        root = nullptr;
    }

    // 기존 root를 새로운 루트로 교체
    root = newRoot;

    updateNodeLengths(root);
}

void Rope::collectLeafNodes(RopeNode* node, std::list<RopeNode*>& leaves) {
    if (!node) return;

    if (node->isLeaf) {
        node->parent = nullptr; // 부모 초기화
        node->left = nullptr;   // 기존 내부 노드 참조 제거
        node->right = nullptr;  // 기존 내부 노드 참조 제거
        leaves.push_back(node);
        return;
    }

    if (node->left) collectLeafNodes(node->left, leaves);
    if (node->right) collectLeafNodes(node->right, leaves);
}

void Rope::collectInternalNodes(RopeNode* node, std::vector<RopeNode*>& internals) {
    if (!node || node->isLeaf) return;

    internals.push_back(node);

    if (node->left) collectInternalNodes(node->left, internals);
    if (node->right) collectInternalNodes(node->right, internals);
}

// 초기 start=0, end=leaves.size()-1로 호출 : 3개면 0, 2
RopeNode* Rope::buildBalancedTree(std::list<RopeNode*>& leaves, int start, int end) {
    // 1️. Base Case: start가 end보다 크다면, 트리가 형성될 수 없으므로 nullptr 반환
    if (start > end) return nullptr;

    auto itStart = leaves.begin();
    std::advance(itStart, start);
    auto itEnd = std::next(itStart, end - start);  // start 기준으로 end까지 이동
    auto itLimit = leaves.end();
    // 2️. Base Case: start와 end가 같다면, 하나의 리프 노드만 존재하는 것이므로 이를 반환
    if (start == end) {
        if (itStart == itLimit) return nullptr;

        (*itStart)->parent = nullptr;  // 부모 초기화 (추후 상위 노드에서 설정)
        (*itStart)->isLeaf = true;      // 리프 노드로 설정
        return (*itStart);  // 단일 리프 노드를 반환
    }

    // 3️. 중간 인덱스 선택 (이진 균형 트리를 만들기 위해 중앙 분할)
    int mid = start + (end - start) / 2;

    // 4️. 새로운 내부 노드 생성 (isLeaf = false)
    RopeNode* node = new RopeNode();
    node->isLeaf = false;
    node->left = nullptr;
    node->right = nullptr;
    node->parent = nullptr;  // 부모 초기화 (추후 상위 노드에서 설정)

    // 5️. 왼쪽 서브트리 구성
    node->left = buildBalancedTree(leaves, start, mid);
    if (node->left) {
        if (node->left == node) {  // 오류 체크: 자기 참조 방지
            node->left = nullptr;
        }
        node->left->parent = node;  // 부모 설정
    }

    // 6️. 오른쪽 서브트리 구성
    node->right = buildBalancedTree(leaves, mid + 1, end);
    if (node->right) {
        if (node->right == node) {  // 오류 체크: 자기 참조 방지
            node->right = nullptr;
        }
        node->right->parent = node;  // 부모 설정
    }

    return node;  // 균형 잡힌 내부 노드 반환
}

size_t Rope::updateNodeLengths(RopeNode* node) {
    if (node == nullptr) return 0;

    // 리프 노드인 경우
    if (node->isLeaf) {
        node->length = node->data.size(); // 문자열 길이를 weight로 설정
        return node->length;
    }

    // 내부 노드: 좌우 자식의 Length를 재귀적으로 계산
    node->length = (node->left != nullptr) ? updateNodeLengths(node->left) : 0;
    size_t rightLength = (node->right != nullptr) ? updateNodeLengths(node->right) : 0;

    return node->length + rightLength;
}

// ---------------------------------------------------
// D2 Render
// ---------------------------------------------------

// COLORREF를 D2D1::ColorF로 변환하는 헬퍼 함수
inline D2D1::ColorF ColorRefToColorF(COLORREF color) {
    return D2D1::ColorF(
        GetRValue(color) / 255.0f,
        GetGValue(color) / 255.0f,
        GetBValue(color) / 255.0f,
        1.0f
    );
}

// D2Render 클래스 구현
D2Render::D2Render()
    : m_fontName(L"Consolas")
    , m_fontSize(16.0f)
    , m_fontWeight(DWRITE_FONT_WEIGHT_NORMAL)
    , m_fontStyle(DWRITE_FONT_STYLE_NORMAL)
    , m_textColor(D2D1::ColorF(D2D1::ColorF::White))
    , m_bgColor(D2D1::ColorF(D2D1::ColorF::Black))
    , m_selectedTextColor(D2D1::ColorF(D2D1::ColorF::White))
    , m_selectedBgColor(D2D1::ColorF(0.0f, 0.4f, 0.8f))
    , m_lineNumColor(D2D1::ColorF(0.7f, 0.7f, 0.7f))
    , m_lineNumBgColor(D2D1::ColorF(0.1f, 0.1f, 0.1f))
    , m_width(0)
    , m_height(0)
    , m_spacing(5)
    , m_initialized(false)
{
    memset(&m_textMetrics, 0, sizeof(TextMetrics));
    m_pRenderTarget = nullptr;
    m_pTextFormat = nullptr;
    m_pTextBrush = nullptr;
    m_pSelectedTextBrush = nullptr;
    m_pSelectedBgBrush = nullptr;
}

D2Render::~D2Render() {
    Shutdown();
}

bool D2Render::Initialize(HWND hwnd) {
    if (m_initialized) {
        return true;
    }

    // Direct2D 팩토리 생성
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
    if (FAILED(hr)) {
        return false;
    }

    // DirectWrite 팩토리 생성
    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&m_pDWriteFactory)
    );
    if (FAILED(hr)) {
        return false;
    }

    // 렌더 타겟 생성을 위한 속성 설정
    RECT rc;
    GetClientRect(hwnd, &rc);
    m_width = rc.right - rc.left;
    m_height = rc.bottom - rc.top;

    // 핵심 렌더링 품질 설정
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0.0f,  // DPI는 기본값
        0.0f,
        D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT
    );

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
        hwnd,
        D2D1::SizeU(m_width, m_height),
        D2D1_PRESENT_OPTIONS_IMMEDIATELY
    );

    // 렌더 타겟 생성
    hr = m_pD2DFactory->CreateHwndRenderTarget(
        rtProps,
        hwndProps,
        &m_pRenderTarget
    );
    if (FAILED(hr)) {
        return false;
    }

    // 텍스트 렌더링 품질 설정
    m_pRenderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    // 텍스트 포맷 생성
    CreateTextFormat();

    // 브러시 생성
    CreateBrushes();

    // 텍스트 메트릭스 계산
    UpdateTextMetrics();
    SetUnifiedBaseline();

    m_initialized = true;
    return true;
}


void D2Render::SetScreenSize(int width, int height) {
    if (m_width != width || m_height != height) {
        m_width = width;
        m_height = height;

        if (m_pRenderTarget) {
            m_pRenderTarget->Resize(D2D1::SizeU(width, height));
        }
    }
}

void D2Render::Clear(COLORREF bgColor) {
    if (!m_initialized || !m_pRenderTarget) {
        return;
    }

    //m_pRenderTarget->Clear(ColorRefToColorF(bgColor));
    // 직접 새 브러시 생성하여 사용
    CComPtr<ID2D1SolidColorBrush> clearBrush;
    D2D1::ColorF clearColor = ColorRefToColorF(bgColor);
    HRESULT hr = m_pRenderTarget->CreateSolidColorBrush(clearColor, &clearBrush);

    if (SUCCEEDED(hr) && clearBrush) {
        D2D1_SIZE_F size = m_pRenderTarget->GetSize();
        D2D1_RECT_F rect = D2D1::RectF(0, 0, size.width, size.height);
        m_pRenderTarget->FillRectangle(rect, clearBrush);
    }
    else {
        TRACE(L"Clear 실패: 브러시 생성 오류 0x%08X\n", hr);
    }
}

void D2Render::BeginDraw() {
    if (!m_initialized || !m_pRenderTarget) {
        return;
    }

    // 렌더 타겟의 상태 확인 추가
    HRESULT windowState = m_pRenderTarget->CheckWindowState();
    if (windowState == D2D1_WINDOW_STATE_OCCLUDED) {
        TRACE(L"경고: 렌더 타겟이 가려져 있습니다\n");
        // 여기서 필요한 처리 추가
    }

    m_pRenderTarget->BeginDraw();
}

void D2Render::EndDraw() {
    if (!m_initialized || !m_pRenderTarget) {
        return;
    }

    HRESULT hr = m_pRenderTarget->EndDraw();
    //if (hr == D2DERR_RECREATE_TARGET) {
    //    // 장치 손실 처리는 상위 클래스에서 처리
    //}
    if (FAILED(hr))
    {
        TRACE(L"Direct2D EndDraw 실패! HRESULT: %x\n", hr);
    }
}

void D2Render::Resize(int width, int height) {
    if (!m_initialized || !m_pRenderTarget) {
        return;
    }

    m_width = width;
    m_height = height;
    m_pRenderTarget->Resize(D2D1::SizeU(width, height));
}

void D2Render::Shutdown() {
    // 모든 COM 인터페이스 해제
    if (m_pLineNumBrush) m_pLineNumBrush.Release();
    if (m_pSelectedBgBrush) m_pSelectedBgBrush.Release();
    if (m_pSelectedTextBrush) m_pSelectedTextBrush.Release();
    if (m_pBgBrush) m_pBgBrush.Release();
    if (m_pTextBrush) m_pTextBrush.Release();
    if (m_pLineNumFormat) m_pLineNumFormat.Release();
    if (m_pTextFormat) m_pTextFormat.Release();
    if (m_pRenderTarget) m_pRenderTarget.Release();
    if (m_pDWriteFactory) m_pDWriteFactory.Release();
    if (m_pD2DFactory) m_pD2DFactory.Release();

    m_initialized = false;
}

void D2Render::SetFont(std::wstring fontName, int fontSize, bool bold, bool italic) {
    m_fontName = fontName;
    m_fontSize = static_cast<float>(fontSize);
    m_fontWeight = bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    m_fontStyle = italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;

    if (m_initialized) {
        CreateTextFormat();
        UpdateTextMetrics();
    }
    SetUnifiedBaseline();
}

void D2Render::SetFontSize(int fontSize) {
    m_fontSize = static_cast<float>(fontSize);
    if (m_initialized) {
        CreateTextFormat();
        UpdateTextMetrics();
    }
    SetUnifiedBaseline();
}

void D2Render::GetFont(std::wstring& fontName, int& fontSize, bool& bold, bool& italic) {
    // 현재 폰트 정보 가져오기
    fontName = m_fontName;
    fontSize = static_cast<int>(m_fontSize);
    bold = (m_fontWeight == DWRITE_FONT_WEIGHT_BOLD);
    italic = (m_fontStyle == DWRITE_FONT_STYLE_ITALIC);
}

int D2Render::GetFontSize() {  
	return m_fontSize;
}

void D2Render::SetSpacing(int spacing) {
    m_spacing = spacing;
    SetUnifiedBaseline();
}

void D2Render::SetTextColor(COLORREF textColor) {
    if (!m_initialized || !m_pRenderTarget) {
        m_textColor = ColorRefToColorF(textColor);
        return;
    }

    m_textColor = ColorRefToColorF(textColor);
    if (m_pTextBrush) {
        m_pTextBrush->SetColor(m_textColor);
    }
}

void D2Render::SetBgColor(COLORREF bgColor) {
    if (!m_initialized || !m_pRenderTarget) {
        m_bgColor = ColorRefToColorF(bgColor);
        return;
    }

    m_bgColor = ColorRefToColorF(bgColor);
    if (m_pBgBrush) {
        m_pBgBrush->SetColor(m_bgColor);
    }
}

void D2Render::SetLineNumColor(COLORREF lineNumColor) {
    if (!m_initialized || !m_pRenderTarget) {
        m_lineNumColor = ColorRefToColorF(lineNumColor);
        return;
    }

    m_lineNumColor = ColorRefToColorF(lineNumColor);
    if (m_pLineNumBrush) {
        m_pLineNumBrush->SetColor(m_lineNumColor);
    }
}

void D2Render::SetLineNumBgColor(COLORREF bgColor) {
    if (!m_initialized || !m_pRenderTarget) {
        m_lineNumBgColor = ColorRefToColorF(bgColor);
        return;
    }
    m_lineNumBgColor = ColorRefToColorF(bgColor);
}

void D2Render::SetSelectionColors(COLORREF textColor, COLORREF bgColor) {
    m_selectedTextColor = ColorRefToColorF(textColor);
    m_selectedBgColor = ColorRefToColorF(bgColor);

    if (m_pSelectedTextBrush) {
        m_pSelectedTextBrush->SetColor(m_selectedTextColor);
    }

    if (m_pSelectedBgBrush) {
        m_pSelectedBgBrush->SetColor(m_selectedBgColor);
    }
}

float D2Render::GetTextWidth(const std::wstring& line) {
    if (!m_initialized || !m_pDWriteFactory || !m_pTextFormat || line.empty()) {
        return 0.0f;
    }

    CComPtr<IDWriteTextLayout> textLayout;
    HRESULT hr = m_pDWriteFactory->CreateTextLayout(
        line.c_str(),
        static_cast<UINT32>(line.length()),
        m_pTextFormat,
        static_cast<float>(m_width * 2),  // 넉넉한 최대 너비
        static_cast<float>(m_textMetrics.lineHeight),
        &textLayout
    );

    if (FAILED(hr) || !textLayout) {
        return 0.0f;
    }

    DWRITE_TEXT_METRICS metrics;
    hr = textLayout->GetMetrics(&metrics);
    if (FAILED(hr)) {
        return 0;
    }
    return metrics.widthIncludingTrailingWhitespace;
}

// 텍스트 내의 각 문자 위치(오프셋)를 픽셀 단위로 측정
std::vector<int> D2Render::MeasureTextPositions(const std::wstring& text) {
    std::vector<int> positions;
    if (!m_initialized || !m_pDWriteFactory || !m_pTextFormat || text.empty()) {
        return positions;
    }

    CComPtr<IDWriteTextLayout> textLayout;
    HRESULT hr = m_pDWriteFactory->CreateTextLayout(
        text.c_str(),
        static_cast<UINT32>(text.length()),
        m_pTextFormat,
        static_cast<float>(m_width * 2),  // 넉넉한 최대 너비
        static_cast<float>(m_textMetrics.lineHeight),
        &textLayout
    );

    if (FAILED(hr) || !textLayout) {
        return positions;
    }

    positions.resize(text.length() + 1, 0);

    for (size_t i = 0; i <= text.length(); ++i) {
        DWRITE_HIT_TEST_METRICS hitTestMetrics;
        float pointX, pointY;
        hr = textLayout->HitTestTextPosition(
            static_cast<UINT32>(i),
            FALSE,
            &pointX,
            &pointY,
            &hitTestMetrics
        );

        if (SUCCEEDED(hr)) {
            positions[i] = static_cast<int>(pointX);
        }
    }

    return positions;
}

TextMetrics D2Render::GetTextMetrics() const {
    return m_textMetrics;
}

float D2Render::GetLineHeight() const {
    return m_textMetrics.lineHeight;
}

void D2Render::FillSolidRect(const D2D1_RECT_F& rect, COLORREF color) {
    if (!m_initialized || !m_pRenderTarget) {
        return;
    }

    CComPtr<ID2D1SolidColorBrush> brush;
    HRESULT hr = m_pRenderTarget->CreateSolidColorBrush(
        ColorRefToColorF(color),
        &brush
    );

    if (SUCCEEDED(hr)) {
        m_pRenderTarget->FillRectangle(rect, brush);
    }
}

void D2Render::DrawEditText(float x, float y, const D2D1_RECT_F* clipRect, const wchar_t* text, size_t length) {
    DrawEditText(x, y, clipRect, text, length, false, 0, length - 1);
}

void D2Render::DrawEditText(float x, float y, const D2D1_RECT_F* clipRect, const wchar_t* text,
    size_t length, bool selected, int startSelectPos, int endSelectPos) {
    // 유효성 검사 추가
    if(!m_initialized || !m_pRenderTarget || !text || length == 0 || !m_pTextFormat) return;

    // 브러시 체크 및 생성
    if (!m_pTextBrush || !m_pSelectedBgBrush) {
        if (!CreateBrushes()) return;
    }

    // 선택 범위 조정
    if (selected && startSelectPos >= endSelectPos) {
        // 전체 텍스트 선택 (기존 동작) : endSelectPos가 -1인 경우 ( 전체선택으로 들어옴 )
        startSelectPos = 0;
        endSelectPos = static_cast<int>(length);
    }

    std::wstring str(text, length);
    std::vector<int> positions = MeasureTextPositions(str);
    float preX = x + (startSelectPos < positions.size() ? positions[startSelectPos] : 0.0f);
    float postX = x + (endSelectPos < positions.size() ? positions[endSelectPos] : GetTextWidth(str));

    if (selected && startSelectPos != endSelectPos) {
        D2D1_RECT_F selRect = D2D1::RectF(preX, y- m_spacing / 2, postX, y + m_textMetrics.lineHeight + (m_spacing-m_spacing/2));
        if (clipRect) {
            selRect.left = max(selRect.left, clipRect->left);
            selRect.top = max(selRect.top, clipRect->top - m_spacing / 2);
            selRect.right = min(selRect.right, clipRect->right);
            selRect.bottom = min(selRect.bottom, clipRect->bottom);
        }

        if (selRect.right > selRect.left && selRect.bottom > selRect.top) {
            m_pRenderTarget->FillRectangle(selRect, m_pSelectedBgBrush);
        }
    }

    // 클리핑 설정
    bool clippingPushed = false;
    if (clipRect) {
        m_pRenderTarget->PushAxisAlignedClip(*clipRect, D2D1_ANTIALIAS_MODE_ALIASED);
        clippingPushed = true;
    }

    // 텍스트 출력 (한 번에)
    D2D1_RECT_F layoutRect = D2D1::RectF(
        x,
        y,
        x + GetTextWidth(str),
        y + m_textMetrics.lineHeight
    );

    try {
        m_pRenderTarget->DrawText(
            text,
            static_cast<UINT32>(length),
            m_pTextFormat,
            layoutRect,
            m_pTextBrush,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
        );
    }
    catch (...) {
        // 예외 무시
    }

    if (clippingPushed) {
        m_pRenderTarget->PopAxisAlignedClip();
    }
}

void D2Render::DrawLineText(float x, float y, const D2D1_RECT_F* clipRect, const wchar_t* text, size_t length) {
    if (!m_initialized || !m_pRenderTarget || !m_pLineNumFormat || !text || length == 0) {
        return;
    }

    // 라인 번호 텍스트 그리기
    D2D1_RECT_F layoutRect = D2D1::RectF(
        x,
        y,
        x + 100.0f,  // 넉넉한 너비
        y + m_textMetrics.lineHeight
    );
    if (clipRect) {
        // 클리핑 적용을 위한 레이어 생성
        D2D1_RECT_F clippedRect = *clipRect;
        m_pRenderTarget->PushAxisAlignedClip(clippedRect, D2D1_ANTIALIAS_MODE_ALIASED);
    }
    m_pRenderTarget->DrawText(
        text,
        static_cast<UINT32>(length),
        m_pLineNumFormat,
        layoutRect,
        m_pLineNumBrush,
        D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
    );
    if (clipRect) {
        // 클리핑 레이어 제거
        m_pRenderTarget->PopAxisAlignedClip();
    }
}

void D2Render::UpdateTextMetrics() {
    if (!m_initialized || !m_pDWriteFactory || !m_pTextFormat) {
        return;
    }

    // 폰트 정보 조회를 위한 텍스트 레이아웃 생성
    CComPtr<IDWriteTextLayout> textLayout;
    HRESULT hr = m_pDWriteFactory->CreateTextLayout(
        L"Aygj|한글",  // 여러 문자를 포함하여 측정
        5,
        m_pTextFormat,
        static_cast<float>(m_width),
        static_cast<float>(m_height),
        &textLayout
    );

    if (FAILED(hr) || !textLayout) {
        return;
    }

    DWRITE_TEXT_METRICS textMetrics;
    hr = textLayout->GetMetrics(&textMetrics);
    if (FAILED(hr)) {
        return;
    }

    DWRITE_LINE_METRICS lineMetrics;
    UINT32 lineCount = 1;
    hr = textLayout->GetLineMetrics(&lineMetrics, 1, &lineCount);
    if (FAILED(hr) || lineCount == 0) {
        return;
    }

    // 폰트 콜렉션 및 폰트 패밀리 가져오기
    CComPtr<IDWriteFontCollection> fontCollection;
    hr = m_pTextFormat->GetFontCollection(&fontCollection);
    if (FAILED(hr) || !fontCollection) {
        return;
    }

    UINT32 fontFamilyIndex;
    BOOL fontFamilyExists;
    hr = fontCollection->FindFamilyName(m_fontName.c_str(), &fontFamilyIndex, &fontFamilyExists);
    if (FAILED(hr) || !fontFamilyExists) {
        return;
    }

    CComPtr<IDWriteFontFamily> fontFamily;
    hr = fontCollection->GetFontFamily(fontFamilyIndex, &fontFamily);
    if (FAILED(hr) || !fontFamily) {
        return;
    }

    // 폰트 정보 가져오기
    CComPtr<IDWriteFont> font;
    hr = fontFamily->GetFirstMatchingFont(m_fontWeight, DWRITE_FONT_STRETCH_NORMAL, m_fontStyle, &font);
    if (FAILED(hr) || !font) {
        return;
    }

    // 메트릭스 가져오기
    DWRITE_FONT_METRICS fontMetrics;
    font->GetMetrics(&fontMetrics);

    // 텍스트 메트릭스 업데이트
    float designUnitsToPixels = m_fontSize / fontMetrics.designUnitsPerEm;

    m_textMetrics.ascent = fontMetrics.ascent * designUnitsToPixels;
    m_textMetrics.descent = fontMetrics.descent * designUnitsToPixels;
    m_textMetrics.lineGap = fontMetrics.lineGap * designUnitsToPixels;
    m_textMetrics.capHeight = fontMetrics.capHeight * designUnitsToPixels;
    m_textMetrics.xHeight = fontMetrics.xHeight * designUnitsToPixels;
    m_textMetrics.underlinePosition = fontMetrics.underlinePosition * designUnitsToPixels;
    m_textMetrics.underlineThickness = fontMetrics.underlineThickness * designUnitsToPixels;
    m_textMetrics.strikethroughPosition = fontMetrics.strikethroughPosition * designUnitsToPixels;
    m_textMetrics.strikethroughThickness = fontMetrics.strikethroughThickness * designUnitsToPixels;
    m_textMetrics.lineHeight = lineMetrics.height;
}

bool D2Render::CreateTextFormat() {
    if (!m_pDWriteFactory) {
        return false;
    }

    // 기존 텍스트 포맷 해제
    m_pTextFormat = nullptr;
    m_pLineNumFormat = nullptr;

    // 메인 텍스트 포맷 생성
    HRESULT hr = m_pDWriteFactory->CreateTextFormat(
        m_fontName.c_str(),
        nullptr,
        m_fontWeight,
        m_fontStyle,
        DWRITE_FONT_STRETCH_NORMAL,
        m_fontSize,
        L"ko-kr",
        &m_pTextFormat
    );

    if (FAILED(hr) || !m_pTextFormat) {
        // 실패 시 기본 폰트 사용
        hr = m_pDWriteFactory->CreateTextFormat(
            L"Arial", // 기본 고정폭 폰트
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            12.0f,
            L"en-us",
            &m_pTextFormat
        );

        if (FAILED(hr) || !m_pTextFormat) {
            return false;
        }
    }

    // 라인번호 텍스트 포맷 생성
    hr = m_pDWriteFactory->CreateTextFormat(
        m_fontName.c_str(),
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD, //m_fontWeight,
        m_fontStyle,
        DWRITE_FONT_STRETCH_NORMAL,
        m_fontSize,
        L"ko-kr",
        &m_pLineNumFormat
    );

    if (FAILED(hr) || !m_pLineNumFormat) {
        // 실패 시 기본 폰트 사용
        hr = m_pDWriteFactory->CreateTextFormat(
            L"Arial", // 기본 고정폭 폰트
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            12.0f,
            L"en-us",
            &m_pLineNumFormat
        );

        if (FAILED(hr) || !m_pLineNumFormat) {
            return false;
        }
    }

    // 텍스트 정렬 설정
    m_pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    m_pTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    m_pLineNumFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_pLineNumFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    m_pLineNumFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    return true;
}

bool D2Render::CreateBrushes() {
    if (!m_pRenderTarget) {
        return false;
    }

    // 텍스트 브러시 생성
    if (!m_pTextBrush) {
        HRESULT hr = m_pRenderTarget->CreateSolidColorBrush(m_textColor, &m_pTextBrush);
        if (FAILED(hr) || !m_pTextBrush) {
            return false;
        }
    }
    else {
        m_pTextBrush->SetColor(m_textColor);
    }

    // 배경 브러시 생성
    if (!m_pBgBrush) {
        HRESULT hr = m_pRenderTarget->CreateSolidColorBrush(m_bgColor, &m_pBgBrush);
        if (FAILED(hr)) {
            // 배경 브러시 실패는 치명적이지 않음
        }
    }
    else {
        m_pBgBrush->SetColor(m_bgColor);
    }

    // 선택된 텍스트 브러시 생성
    if (!m_pSelectedTextBrush) {
        HRESULT hr = m_pRenderTarget->CreateSolidColorBrush(m_selectedTextColor, &m_pSelectedTextBrush);
        if (FAILED(hr)) {
            // 선택된 텍스트 브러시 실패는 치명적이지 않음
        }
    }
    else {
        m_pSelectedTextBrush->SetColor(m_selectedTextColor);
    }

    // 선택된 배경 브러시 생성
    if (!m_pSelectedBgBrush) {
        HRESULT hr = m_pRenderTarget->CreateSolidColorBrush(m_selectedBgColor, &m_pSelectedBgBrush);
        if (FAILED(hr)) {
            // 선택된 배경 브러시 실패는 치명적이지 않음
        }
    }
    else {
        m_pSelectedBgBrush->SetColor(m_selectedBgColor);
    }

    // 라인넘버 브러시 생성
    if (!m_pLineNumBrush) {
        HRESULT hr = m_pRenderTarget->CreateSolidColorBrush(m_lineNumColor, &m_pLineNumBrush);
        if (FAILED(hr) || !m_pLineNumBrush) {
            return false;
        }
    }
    else {
        m_pLineNumBrush->SetColor(m_lineNumColor);
    }

    return true; // 텍스트 브러시가 생성됨
}

void D2Render::SetUnifiedBaseline() {
    if (!m_pTextFormat) return;

    // 통합 베이스라인 계산 (75% 지점)
    float unifiedLineSpacing = m_textMetrics.lineHeight + m_spacing;
    float unifiedBaseline = m_textMetrics.lineHeight*0.8f;
    //float unifiedBaseline = m_textMetrics.lineHeight;

    HRESULT hr = m_pTextFormat->SetLineSpacing(
        DWRITE_LINE_SPACING_METHOD_UNIFORM,
        unifiedLineSpacing,
        unifiedBaseline
    );

    if (FAILED(hr)) {
        // 로그 또는 에러 처리
        OutputDebugStringA("Failed to set unified baseline\n");
    }
}

void D2Render::LogRenderTargetState() {
    TRACE(L"--- D2Render 상태 ---\n");
    TRACE(L"초기화 상태: %s\n", m_initialized ? L"초기화됨" : L"초기화되지 않음");
    TRACE(L"렌더타겟: %p\n", m_pRenderTarget.p);

    if (m_pRenderTarget) {
        D2D1_SIZE_F size = m_pRenderTarget->GetSize();
        TRACE(L"렌더타겟 크기: 너비=%.1f, 높이=%.1f\n", size.width, size.height);

        HRESULT testState = m_pRenderTarget->CheckWindowState();
        TRACE(L"윈도우 상태: %s (0x%08X)\n",
            (testState == D2D1_WINDOW_STATE_OCCLUDED) ? L"가려짐" :
            (testState == S_OK) ? L"정상" : L"기타",
            testState);
    }
    TRACE(L"----------------------\n");
}
