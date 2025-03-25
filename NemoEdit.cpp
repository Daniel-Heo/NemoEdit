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

// NemoEdit 클래스 생성자 - 기본 초기화
NemoEdit::NemoEdit()
    : m_isReadOnly(false), m_showLineNumbers(true),
      m_wordWrap(true), m_wordWrapWidth(0),
      m_lineSpacing(5),
	  m_margin({ 5, 15, 5, 0 }),
      m_lineHeight(0), m_charWidth(0),
	  m_scrollX(0), m_scrollYLine(0), m_scrollYWrapLine(0),
      m_nextDiffNum(0),
	  m_isUseScrollCtrl(FALSE), m_showScrollBars(FALSE)
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
	
	// undo, redo 스택 초기화
    m_undoStack.reserve(100);
    m_redoStack.reserve(100);

    // IME
	m_imeComposition.isComposing = false;

#ifdef _DEBUG
    // 디버그 모드에서만 실행되는 코드
    _CrtSetDbgFlag(0); // 메모리 누수 보고 비활성화 : 대용량 시에 기다리는 시간이 아까워서.
#endif
}

// 소멸자
NemoEdit::~NemoEdit() {
}

// 윈도우 클래스 등록 및 컨트롤 생성
BOOL NemoEdit::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID) {
    LPCTSTR className = AfxRegisterWndClass(CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW,
                                           ::LoadCursor(NULL, IDC_IBEAM),
                                           NULL, NULL);
    // 기본 스타일 설정 (자식 윈도우, 스크롤바 포함) - WS_CLIPCHILDREN 추가하여 자식 윈도우 영역 그리기 방지
    dwStyle |= WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL  | WS_CLIPCHILDREN;
    BOOL res = CWnd::CreateEx(WS_EX_COMPOSITED, className, _T(""), dwStyle,
                          rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                          pParentWnd->GetSafeHwnd(), (HMENU)nID);

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

    CClientDC dc(this);
    dc.SelectObject(&m_font);
    return dc.GetTextExtent(line.c_str(), line.length()).cx;
}

// 현재 화면에 표시되는 라인 중 가장 긴 라인의 너비를 계산
int NemoEdit::GetMaxWidth() {
    int maxWidth = 0;

    CRect client;
    GetClientRect(&client);
    int visibleLines =( client.Height()-m_margin.top-m_margin.bottom) / m_lineHeight;

    // 시작 라인과 끝 라인 결정 (모드에 따라 다름)
    int startLine = 0;
    int endLine = 0;

    // 통합 모드: m_scrollYLine부터 계산
    startLine = m_scrollYLine;
    endLine = min(static_cast<int>(m_rope.getSize() - 1), startLine + visibleLines);


    // 화면에 보이는 라인들만 계산
    for (int i = startLine; i <= endLine; i++) {
        int lineWidth = GetLineWidth(i);
        if (lineWidth > maxWidth) {
            maxWidth = lineWidth;
        }
    }

    return maxWidth;
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
    // 기본 폰트 설정 (시스템 기본 GUI 폰트 사용)
    CFont* pDefaultFont = CFont::FromHandle((HFONT)::GetStockObject(DEFAULT_GUI_FONT));
    LOGFONT lf;
    pDefaultFont->GetLogFont(&lf);
    m_font.CreateFontIndirect(&lf);
    // 폰트 메트릭 계산
    CClientDC dc(this);
    dc.SelectObject(&m_font);
    TEXTMETRIC tm;
    dc.GetTextMetrics(&tm);
    m_lineHeight = tm.tmHeight + tm.tmExternalLeading + m_lineSpacing;
    m_charWidth = tm.tmAveCharWidth; // 평균 문자 넓이를 적용한다.
    // 더블 버퍼링용 메모리 DC/비트맵 생성
    CRect client;
    GetClientRect(&client);
    m_memDC.CreateCompatibleDC(&dc);
    m_memBitmap.CreateCompatibleBitmap(&dc, max(1, client.Width()), max(1, client.Height()));
    m_memDC.SelectObject(&m_memBitmap);
    m_memSize = client.Size();
    m_memDC.SelectObject(&m_font);
    // Drag & Drop 지원을 원할 경우 OLE 드롭타겟 등록 (생략 가능)
    // COleDropTarget* pDropTarget = new COleDropTarget();
    // pDropTarget->Register(this);
    return 0;
}

void NemoEdit::SetFont(std::wstring fontName, int fontSize, bool bold, bool italic) {
	LOGFONT lf;
	memset(&lf, 0, sizeof(LOGFONT));
	wcscpy_s(lf.lfFaceName, fontName.c_str());
	lf.lfHeight = -fontSize;
	lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
	lf.lfItalic = italic;
	SetFont(lf);
}

// 폰트 변경 (LOGFONT 사용)
void NemoEdit::SetFont(const LOGFONT& lf) {
    m_font.DeleteObject();
    m_font.CreateFontIndirect(&lf);
    // 폰트 메트릭 갱신
    CClientDC dc(this);
    dc.SelectObject(&m_font);
    TEXTMETRIC tm;
    dc.GetTextMetrics(&tm);
    m_lineHeight = tm.tmHeight + tm.tmExternalLeading + m_lineSpacing;
    // 메모리 DC에도 새 폰트 선택
    m_memDC.SelectObject(&m_font);
    m_charWidth = GetTextWidth(L"08")/2; // 공백 문자 너비로 대체
    m_nextDiffNum = 0; // numLineArea 재계산
    RecalcScrollSizes();
    Invalidate(FALSE);
}

// 폰트 변경 (기존 CFont 객체 사용)
void NemoEdit::SetFont(CFont* pFont) {
    LOGFONT lf;
    if(pFont) {
        pFont->GetLogFont(&lf);
    } else {
        CFont* pDefaultFont = CFont::FromHandle((HFONT)::GetStockObject(DEFAULT_GUI_FONT));
        pDefaultFont->GetLogFont(&lf);
    }
    SetFont(lf);
}

// 추가 줄 간격 설정
void NemoEdit::SetLineSpacing(int spacing) {
    m_lineSpacing = spacing;
    // 라인 높이 갱신
    CClientDC dc(this);
    dc.SelectObject(&m_font);
    TEXTMETRIC tm;
    dc.GetTextMetrics(&tm);
    m_lineHeight = tm.tmHeight + tm.tmExternalLeading + m_lineSpacing;
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
	Invalidate(FALSE);
}

// 라인 번호 색상 설정
void NemoEdit::SetLineNumColor(COLORREF lineNumColor, COLORREF bgColor) {
	m_colorInfo.lineNum = lineNumColor;
	m_colorInfo.lineNumBg = bgColor;
	Invalidate(FALSE);
}

// 에디터 전체 텍스트 설정
void NemoEdit::SetText(const std::wstring& text) {
    // 화면 갱신 일시 중지
    SetRedraw(FALSE);
    ClearText();

    // 개행 기준으로 문자열 파싱
    std::deque<std::wstring> lines;
    size_t pos = 0;

    while (pos < text.size()) {
        size_t nlPos = text.find(L'\n', pos);
        std::wstring line;

        if (nlPos == std::wstring::npos) {
            line = text.substr(pos);
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();  // CR 제거
            }
            lines.push_back(line);
            break;
        }
        else {
            line = text.substr(pos, nlPos - pos);
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();
            }
            lines.push_back(line);
            pos = nlPos + 1;
        }
    }

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

    // Undo/Redo 스택 초기화
    m_undoStack.clear();
    m_redoStack.clear();

    UpdateCaretPosition(); // 케럿 초기화 적용
    RecalcScrollSizes();
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
    std::deque<std::wstring> lines;
    size_t pos = 0;

    while (pos < text.size()) {
        size_t nlPos = text.find(L'\n', pos);
        std::wstring line;

        if (nlPos == std::wstring::npos) {
            line = text.substr(pos);
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();  // CR 제거
            }
            lines.push_back(line);
            break;
        }
        else {
            line = text.substr(pos, nlPos - pos);
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();
            }
            lines.push_back(line);
            pos = nlPos + 1;
        }
    }

    // 텍스트가 없으면 종료
    if (lines.empty()) {
        return;
    }

    // 추가 위치는 마지막 라인 다음 (마지막 라인의 인덱스 + 1)
    size_t insertIndex = m_rope.getSize();

    // Undo 레코드 준비
    UndoRecord rec;
    rec.type = UndoRecord::Insert;
    rec.start = TextPos(insertIndex, 0);
    rec.text = text;  // 원본 텍스트 그대로 저장

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
    insertIndex = m_rope.getSize() - 1;
    m_caretPos.lineIndex = insertIndex;
    m_caretPos.column = lines.back().length();

    // 선택 영역 초기화
    m_selectInfo.start = m_selectInfo.end = m_selectInfo.anchor = m_caretPos;
    m_selectInfo.isSelected = false;
    m_selectInfo.isSelecting = false;

    // Undo/Redo 스택 갱신
    m_undoStack.push_back(rec);
    m_redoStack.clear();

    // 화면 갱신 및 커서 위치 보정
    EnsureCaretVisible();
    RecalcScrollSizes();
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
    catch (std::exception& e) {
        // 예외 처리: 메모리 부족 등의 오류 발생 시
        MessageBeep(MB_ICONERROR);
    }
}

// 클립보드의 텍스트를 현 캐럿 위치에 붙여넣기
void NemoEdit::Paste() {
    if (m_isReadOnly) return;

    std::wstring clipText = LoadClipText();

    // 캐리지리턴(\r) 제거하여 '\n'만 남김
    clipText.erase(std::remove(clipText.begin(), clipText.end(), L'\r'), clipText.end());
    if (clipText.empty()) {
        return;
    }

    // 붙여넣기 전 선택 영역이 있으면 삭제 (덮어쓰기)
    if (m_selectInfo.isSelected) {
        DeleteSelection();
    }

    TextPos insertPos = m_caretPos;

    // 삽입 위치 유효성 검사
    if (insertPos.lineIndex < 0)
        insertPos.lineIndex = 0;
    if( insertPos.lineIndex >= (int)m_rope.getSize()) 
        insertPos.lineIndex = (int)m_rope.getSize();

    // Undo 레코드 준비 - 모든 텍스트에 대해 하나의 레코드만 생성
    UndoRecord rec;
    rec.type = UndoRecord::Insert;
    rec.start = insertPos;
    rec.text = clipText;  // 원본 텍스트 그대로 저장 (줄바꿈 포함)

    // 텍스트를 줄바꿈으로 분리
    std::deque<std::wstring> lines;
    size_t start = 0, pos;
    while ((pos = clipText.find(L'\n', start)) != std::wstring::npos) {
        lines.push_back(clipText.substr(start, pos - start));
        start = pos + 1;
    }
    if (start < clipText.length()) {
        lines.push_back(clipText.substr(start));
    }
    else if (!clipText.empty() && clipText.back() == L'\n') {
        lines.push_back(L"");
    }

    // 현재 라인의 캐럿 이후 부분 저장
    std::wstring tail;
	std::wstring startLine = m_rope.getLine(insertPos.lineIndex);
    if (insertPos.column < (int)startLine.size()) {
        tail = startLine.substr(insertPos.column);
        startLine.erase(insertPos.column);
    }
    
    lines.front() = startLine + lines.front(); // 첫 번째 라인 처리
    lines.back() += tail; // 마지막 라인에 tail 추가
    // 첫번째 라인 처리
    m_rope.update(insertPos.lineIndex, lines.front());

    bool isMultiline = false;
    if (lines.size() > SPLIT_THRESHOLD) {
		isMultiline = true;
        lines.pop_front();
        // 나머지 라인들은 insertMultiple로 한 번에 삽입
        if (!lines.empty()) {
            m_rope.insertMultiple(insertPos.lineIndex + 1, lines);
        }
    }
    else {
        // 나머지 라인들은 개별적으로 삽입
        for (size_t i = 1; i < lines.size(); i++) {
            m_rope.insert(insertPos.lineIndex + i, lines[i]);
        }
    }

    // 캐럿 위치 갱신
	if (lines.size() > 1) { // 여러 라인 삽입
		if (isMultiline) m_caretPos.lineIndex = insertPos.lineIndex + lines.size(); // pop_front 했으므로 +1
		else m_caretPos.lineIndex = insertPos.lineIndex + lines.size() - 1;
    }
	else { // 단일 라인 삽입
        m_caretPos.lineIndex = insertPos.lineIndex;
    }
    m_caretPos.column = lines.back().length() - tail.length();

    // Undo/Redo 스택 갱신 - 하나의 작업으로 기록
    m_undoStack.push_back(rec);
    m_redoStack.clear();

    // 화면 갱신 및 커서 위치 보정
    EnsureCaretVisible();
    RecalcScrollSizes();
    Invalidate(FALSE);
}

// Undo 실행
void NemoEdit::Undo() {
    // Undo 스택이 비어있으면 종료
    if (m_undoStack.empty() || m_isReadOnly) return;

    // 마지막 작업 기록 가져오기
    UndoRecord rec = m_undoStack.back();
    m_undoStack.pop_back();

    // 캐럿 위치를 작업 시작 지점으로 설정
    m_caretPos = rec.start;

    // 작업 유형에 따른 처리
    if (rec.type == UndoRecord::Insert) {
        // 삽입 취소 (삭제 작업 수행)
        std::vector<std::wstring> parts;
        SplitTextByNewlines(rec.text, parts);

        // 단일 라인 삽입 취소
        if (parts.size() == 1) {
            // 단일 텍스트 삭제
            m_rope.eraseAt(rec.start.lineIndex, rec.start.column, parts[0].length());
        }
        // 여러 라인 삽입 취소
        else if (parts.size() > 1) {
            // 첫 번째 라인의 원래 텍스트 저장
            std::wstring originalLine = m_rope.getLine(rec.start.lineIndex).substr(0, rec.start.column);

            // 마지막 라인의 꼬리 부분 저장
            int lastLineIdx = rec.start.lineIndex + parts.size() - 1;
            std::wstring lastLineTail;
            if (lastLineIdx < static_cast<int>(m_rope.getSize())) {
                std::wstring lastLine = m_rope.getLine(lastLineIdx);
                if (parts.size() > 1 && parts.back().length() < lastLine.length()) {
                    lastLineTail = lastLine.substr(parts.back().length());
                }
            }

            // 첫 번째 라인 수정 (텍스트 삭제 후 꼬리 추가)
            m_rope.update(rec.start.lineIndex, originalLine + lastLineTail);

            // 추가된 라인들 삭제 (역순으로 삭제)
            for (int i = lastLineIdx; i > rec.start.lineIndex; i--) {
                m_rope.erase(i);
            }
        }
    }
    else if (rec.type == UndoRecord::Delete) {
        // 삭제 취소 (다시 삽입)
        std::vector<std::wstring> parts;
        SplitTextByNewlines(rec.text, parts);

        // 단일 라인 텍스트 복원
        if (parts.size() == 1) {
            m_rope.insertAt(rec.start.lineIndex, rec.start.column, parts[0]);
            // 캐럿 위치 조정
            m_caretPos.column = rec.start.column + parts[0].length();
        }
        // 여러 라인 텍스트 복원
        else if (parts.size() > 1) {
            // 현재 라인의 텍스트 가져오기
            std::wstring currentLine = m_rope.getLine(rec.start.lineIndex);

            // 현재 라인을 분할: 앞부분과 뒷부분
            std::wstring headText = currentLine.substr(0, rec.start.column);
            std::wstring tailText = currentLine.substr(rec.start.column);

            // 첫 번째 라인 업데이트 (앞부분 + 복원할 첫 줄)
            m_rope.update(rec.start.lineIndex, headText + parts[0]);

            // 중간 라인들 삽입
            for (size_t i = 1; i < parts.size() - 1; i++) {
                m_rope.insert(rec.start.lineIndex + i, parts[i]);
            }

            // 마지막 라인 삽입 (마지막 복원 줄 + 뒷부분)
            if (parts.size() > 1) {
                m_rope.insert(rec.start.lineIndex + parts.size() - 1, parts.back() + tailText);
            }

            // 캐럿 위치 조정
            m_caretPos.lineIndex = rec.start.lineIndex + parts.size() - 1;
            m_caretPos.column = parts.back().length();
        }
    }

    // Redo 스택에 추가
    m_redoStack.push_back(rec);

    // 화면 갱신
    EnsureCaretVisible();
    RecalcScrollSizes();
    Invalidate(FALSE);
}

// Redo 실행
void NemoEdit::Redo() {
    // Redo 스택이 비어있으면 종료
    if (m_redoStack.empty() || m_isReadOnly) return;

    // 마지막 작업 기록 가져오기
    UndoRecord rec = m_redoStack.back();
    m_redoStack.pop_back();

    // 캐럿 위치를 작업 시작 지점으로 설정
    m_caretPos = rec.start;

    // 작업 유형에 따른 처리 (Undo의 반대 작업 수행)
    if (rec.type == UndoRecord::Insert) {
        // 삽입 다시 실행
        std::vector<std::wstring> parts;
        SplitTextByNewlines(rec.text, parts);

        // 단일 라인 삽입
        if (parts.size() == 1) {
            m_rope.insertAt(rec.start.lineIndex, rec.start.column, parts[0]);
            // 캐럿 위치 조정
            m_caretPos.column = rec.start.column + parts[0].length();
        }
        // 여러 라인 삽입
        else if (parts.size() > 1) {
            // 현재 라인의 텍스트 가져오기
            std::wstring currentLine = m_rope.getLine(rec.start.lineIndex);

            // 현재 라인을 분할: 앞부분과 뒷부분
            std::wstring headText = currentLine.substr(0, rec.start.column);
            std::wstring tailText = currentLine.substr(rec.start.column);

            // 첫 번째 라인 업데이트 (앞부분 + 첫 줄)
            m_rope.update(rec.start.lineIndex, headText + parts[0]);

            // 중간 라인들 삽입
            for (size_t i = 1; i < parts.size() - 1; i++) {
                m_rope.insert(rec.start.lineIndex + i, parts[i]);
            }

            // 마지막 라인 삽입 (마지막 줄 + 뒷부분)
            if (parts.size() > 1) {
                m_rope.insert(rec.start.lineIndex + parts.size() - 1, parts.back() + tailText);
            }

            // 캐럿 위치 조정
            m_caretPos.lineIndex = rec.start.lineIndex + parts.size() - 1;
            m_caretPos.column = parts.back().length();
        }
    }
    else if (rec.type == UndoRecord::Delete) {
        // 삭제 다시 실행
        std::vector<std::wstring> parts;
        SplitTextByNewlines(rec.text, parts);

        // 단일 라인 삭제
        if (parts.size() == 1) {
            m_rope.eraseAt(rec.start.lineIndex, rec.start.column, parts[0].length());
        }
        // 여러 라인 삭제
        else if (parts.size() > 1) {
            // 첫 번째 라인의 원래 텍스트 저장
            std::wstring originalLine = m_rope.getLine(rec.start.lineIndex).substr(0, rec.start.column);

            // 마지막 라인의 꼬리 부분 저장
            int lastLineIdx = rec.start.lineIndex + parts.size() - 1;
            std::wstring lastLineTail;
            if (lastLineIdx < static_cast<int>(m_rope.getSize())) {
                std::wstring lastLine = m_rope.getLine(lastLineIdx);
                if (parts.size() > 1 && parts.back().length() < lastLine.length()) {
                    lastLineTail = lastLine.substr(parts.back().length());
                }
            }

            // 첫 번째 라인 수정 (텍스트 삭제 후 꼬리 추가)
            m_rope.update(rec.start.lineIndex, originalLine + lastLineTail);

            // 추가된 라인들 삭제 (역순으로 삭제)
            for (int i = lastLineIdx; i > rec.start.lineIndex; i--) {
                m_rope.erase(i);
            }
        }
    }

    // Undo 스택에 추가
    m_undoStack.push_back(rec);

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
                    int lineSize = m_rope.getLineSize(m_caretPos.lineIndex);
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
                    int lineSize = m_rope.getLineSize(m_caretPos.lineIndex);
                    if (m_caretPos.lineIndex<m_rope.getSize() && desiredCol >lineSize) {
                        desiredCol = lineSize;
                    }
                    m_caretPos.column = desiredCol;
                }
                else {
                    // 이미 마지막 라인이면 라인 끝으로만 이동
                    int lineSize = m_rope.getLineSize(m_caretPos.lineIndex);
                    m_caretPos.column = (m_caretPos.lineIndex < m_rope.getSize() ? lineSize : 0);
                    break;
                }
            }
        }
    }
}

// 텍스트 분할
void NemoEdit::SplitTextByNewlines(const std::wstring& text, std::vector<std::wstring>& parts) {
    parts.clear();

    // 대량 텍스트인 경우 미리 메모리 할당
    if (text.length() > 10000) {
        // 대략적인 라인 수 추정 (평균 라인 길이를 50자로 가정)
        size_t estimatedLines = text.length() / 50 + 100;
        parts.reserve(estimatedLines);
    }

    size_t start = 0;
    size_t pos = 0;

    // 문자열 전체를 한 번만 순회
    while (pos < text.length()) {
        if (text[pos] == L'\n') {
            // 라인 추출 (CR 제거)
            size_t lineEnd = pos;
            if (pos > 0 && text[pos - 1] == L'\r') {
                lineEnd--;
            }

            // 처음부터 새 라인 생성하지 않고 사이즈만 미리 계산
            size_t lineLength = lineEnd - start;

            // 문자열 복사 최소화 (substr 한 번만 호출)
            parts.push_back(text.substr(start, lineLength));

            // 다음 시작 위치 설정
            start = pos + 1;
        }
        pos++;
    }

    // 마지막 라인 처리
    if (start < text.length()) {
        parts.push_back(text.substr(start));
    }

    // 텍스트가 비어있거나 마지막이 개행문자인 경우 빈 라인 추가
    if (parts.empty() || (text.length() > 0 && text.back() == L'\n')) {
        parts.push_back(L"");
    }
}

// 새로운 문자를 현 위치에 삽입
void NemoEdit::InsertChar(wchar_t ch) {
	std::wstring addStr(1, ch);
	m_rope.insertAt(m_caretPos.lineIndex, m_caretPos.column, addStr);
 
    // Undo 레코드 추가
    UndoRecord rec;
    rec.type = UndoRecord::Insert;
    rec.start = m_caretPos;
    rec.text = addStr;
    m_undoStack.push_back(rec);
    m_redoStack.clear();
    // 캐럿 이동
    m_caretPos.column++;
}

// 새 줄 삽입 (현재 위치에서 줄 분리)
void NemoEdit::InsertNewLine() {
    int oldLineIdx = m_caretPos.lineIndex;
    int oldCol = m_caretPos.column;

	std::wstring text = m_rope.getLine(oldLineIdx);
    std::wstring tail = text.substr(oldCol); // 뒤의 문자열 저장
	std::wstring chgText = text.substr(0, oldCol); // 캐럿 이전 문자열 저장
	m_rope.update(oldLineIdx, chgText); // 현재 라인에서 캐럿 이전 문자열 저장
	m_rope.insert(oldLineIdx + 1, tail); // 현재 라인 뒤에 tail을 담은 새 라인 추가

    // 캐럿 위치 새로운 라인으로
    m_caretPos.lineIndex = oldLineIdx + 1;
    m_caretPos.column = 0;
    m_scrollX = 0;

    // Undo 레코드 추가 (개행 삽입)
    UndoRecord rec;
    rec.type = UndoRecord::Insert;
    rec.start = TextPos(oldLineIdx, oldCol);
    rec.text = L"\n";
    m_undoStack.push_back(rec);
    m_redoStack.clear();
}

// 문자 삭제 (backspace=true인 경우 Backspace 처리, false이면 Delete 처리)
void NemoEdit::DeleteChar(bool backspace) {
    // 선택 영역이 있을 경우 해당 영역 삭제
    if(m_selectInfo.isSelected) {
        DeleteSelection();
        return;
    }

	// 백스페이스 키 처리
    if(backspace) {
        // 이전 라인과 병합
        if(m_caretPos.column == 0) {
            // 라인 맨 앞에서 백스페이스 -> 이전 라인과 병합
            if(m_caretPos.lineIndex > 0) {
                int prevLength = m_rope.getLineSize(m_caretPos.lineIndex - 1);
                m_rope.mergeLine(m_caretPos.lineIndex-1);

                // Undo: 개행 문자 제거
                UndoRecord rec;
                rec.type = UndoRecord::Delete;
                rec.start = TextPos(m_caretPos.lineIndex - 1, prevLength);
                rec.text = L"\n";

                // 캐럿을 이전 라인의 끝으로 이동
                m_caretPos.lineIndex--;
                m_caretPos.column = prevLength;
                m_undoStack.push_back(rec);
                m_redoStack.clear();
            }
        }
        // 단순 삭제
        else {
            // 같은 라인에서 캐럿 왼쪽 문자 삭제
			m_rope.eraseAt(m_caretPos.lineIndex, m_caretPos.column-1, 1);
			std::wstring removeStr = m_rope.getLine(m_caretPos.lineIndex).substr(m_caretPos.column-1, 1);
            m_caretPos.column--;
            // Undo: 문자 삭제
            UndoRecord rec;
            rec.type = UndoRecord::Delete;
            rec.start = TextPos(m_caretPos.lineIndex, m_caretPos.column);
            rec.text = removeStr;
            m_undoStack.push_back(rec);
            m_redoStack.clear();
        }
    }
	// Delete 키 처리
    else {
        // Delete 키 처리
        if(m_caretPos.column >= (int)m_rope.getLineSize(m_caretPos.lineIndex)) {
            // 라인 끝에서 Delete -> 다음 라인과 병합
            if(m_caretPos.lineIndex < (int)m_rope.getSize() - 1) {
				m_rope.mergeLine(m_caretPos.lineIndex);

                // Undo: 개행 삭제
                UndoRecord rec;
                rec.type = UndoRecord::Delete;
                rec.start = m_caretPos;
                rec.text = L"\n";
                m_undoStack.push_back(rec);
                m_redoStack.clear();
                // 캐럿 위치는 변동 없음 (병합된 위치가 원래 캐럿 위치)
            }
        } else {
            // 캐럿 위치의 문자 삭제
            m_rope.eraseAt(m_caretPos.lineIndex, m_caretPos.column, 1);
            std::wstring removeStr = m_rope.getLine(m_caretPos.lineIndex).substr(m_caretPos.column, 1);
            UndoRecord rec;
            rec.type = UndoRecord::Delete;
            rec.start = m_caretPos;
            rec.text = removeStr;
            m_undoStack.push_back(rec);
            m_redoStack.clear();
        }
    }
}

void NemoEdit::CancelSelection() {
    m_selectInfo.isSelected = false;
	m_selectInfo.isSelecting=false;
	m_selectInfo.start.lineIndex = m_selectInfo.end.lineIndex = m_caretPos.lineIndex;
    m_selectInfo.start.column = m_selectInfo.end.column = m_caretPos.column;
}

// 선택 영역의 텍스트 삭제
void NemoEdit::DeleteSelection() {
    // 선택 영역이 없으면 종료
    if (!m_selectInfo.isSelected || m_isReadOnly)
        return;

    // 선택 영역 정규화
    TextPos start = m_selectInfo.start;
    TextPos end = m_selectInfo.end;
    if (end.lineIndex < start.lineIndex ||
        (end.lineIndex == start.lineIndex && end.column < start.column)) {
        start = m_selectInfo.end;
        end = m_selectInfo.start;
    }

    // 삭제될 텍스트 저장 (Undo를 위해)
    std::wstring removedText;

    // 선택 영역이 한 라인 내에 있는 경우
    if (start.lineIndex == end.lineIndex) {
		std::wstring text = m_rope.getLine(start.lineIndex);
        removedText = text.substr(start.column, end.column - start.column);
        text.erase(start.column, end.column - start.column);
		m_rope.update(start.lineIndex, text);
    }
    // 선택 영역이 여러 라인에 걸쳐 있는 경우
    else {
		removedText = m_rope.getTextRange(start.lineIndex, start.column, end.lineIndex, end.column);
		m_rope.eraseAt(start.lineIndex, start.column, m_rope.getLineSize(start.lineIndex) - start.column);
		m_rope.eraseAt(end.lineIndex, 0, end.column);
		if (end.lineIndex - start.lineIndex > 1)
		    m_rope.eraseRange(start.lineIndex+1, end.lineIndex - start.lineIndex-1);
		m_rope.mergeLine(start.lineIndex);
    }

    // 캐럿을 시작 지점으로 이동
    m_caretPos = start;

    // 선택 영역 해제
    m_selectInfo.start = m_caretPos;
    m_selectInfo.end = m_caretPos;
    m_selectInfo.anchor = m_caretPos;
    m_selectInfo.isSelected = false;
    m_selectInfo.isSelecting = false;

    // Undo 레코드 추가
    UndoRecord rec;
    rec.type = UndoRecord::Delete;
    rec.start = start;
    rec.text = removedText;
    m_undoStack.push_back(rec);
    m_redoStack.clear();

    // 모든 텍스트가 삭제된 경우 빈 라인 추가
    if (m_rope.empty()) {
        m_rope.insertBack(L"");
        m_caretPos = TextPos(0, 0);
        m_selectInfo.start = m_selectInfo.end = m_selectInfo.anchor = m_caretPos;
        m_selectInfo.isSelected = false;
        m_selectInfo.isSelecting = false;
    }

	EnsureCaretVisible();
    RecalcScrollSizes();
    Invalidate(FALSE);
}

// 최적화된 라인 너비 계산
int NemoEdit::GetTextWidth(const std::wstring& line) {
    return m_memDC.GetTextExtent(line.c_str(), line.length()).cx;
}

// lineIndex: 라인 인덱스
std::vector<int> NemoEdit::FindWordWrapPosition(int lineIndex){
    std::vector<int> wrapPos;

    std::wstring lineText = m_rope.getLine(lineIndex);
    if (lineText.empty()) return {}; // 빈 줄일 경우 워드랩 필요 없음

    // 한 줄 전체가 들어갈 경우
    int lineWidth = GetTextWidth(lineText);
    if (lineWidth <= m_wordWrapWidth) {
        return {};
    }
    

    int currentPos = 0;
    int currWidthSum = 0;
    while (currentPos < (int)lineText.length()) {
        // 이진 검색으로 현재 위치에서 가장 텍스트 찾기
        int low = 1;
        int high = lineText.length() - currentPos;
        int result = 1; // 기본값
        int currWidth = 0;

        while (low <= high) {
            int mid = (low + high) / 2;
            if (mid <= 0) mid = 1; // 보호 코드

            int testSize = GetTextWidth(lineText.substr(currentPos, mid));

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

    // 화면 표시 영역 크기 계산
    CRect client;
    GetClientRect(&client);
    int screenWidth = client.Width() - m_margin.left - m_margin.right - CalculateNumberAreaWidth();
    int screenHeight = client.Height() - m_margin.top - m_margin.bottom;
    int visibleLines = screenHeight / m_lineHeight; // 마진을 제외한 화면에 보이는 줄 수 ( top margin만 계산 )
    m_wordWrapWidth = screenWidth;

    // 워드랩 모드일 때 다른 계산 방식 사용
    if (m_wordWrap) {
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
		if (lineIndex< m_scrollYLine || 
            (lineIndex== m_scrollYLine&& wrapLineIndex< m_scrollYWrapLine)) {
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
                totalLines += FindWordWrapPosition(i).size()+1;
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
        if ( !line.empty()  && pos.column >= startCol) {
			if (pos.column == startCol) {
				pt.x = 0;
			}
            else {
                std::wstring text = line.substr(startCol, pos.column - startCol);
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
        pt.y = (lineIndex - m_scrollYLine) * m_lineHeight;

        // 수평 위치: 해당 라인의 문자 폭 계산
        auto line = m_rope.getLine(lineIndex);
        if (!line.empty()) {
            if (pos.column > 0) {
                std::wstring text = line.substr(0, pos.column);
                pt.x = GetTextWidth(text);
            }
            else {
                pt.x = 0;
            }
        }
    }

    pt.x -= m_scrollX;

    return pt;
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
        int lineSize = m_rope.getSize();
        int prevY = 0;
        std::vector<int> wrapCols;
        int wrapColsSize = 0;
        int startCol = 0;
        int totalPrevLines = -m_scrollYWrapLine;
        for (int i = m_scrollYLine; i < lineSize; i++) {
            wrapCols = FindWordWrapPosition(i);
            totalPrevLines += wrapCols.size() + 1;
            prevY = totalPrevLines * m_lineHeight+m_margin.top;
            pos.lineIndex = i;
            if (prevY >= pt.y) {
                pos.lineIndex = i;
                // wrapLineIndex 계산
                wrapColsSize = wrapCols.size();
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
        int col = 0;
        if (!lineText.empty()) {
            // 이진 검색으로 텍스트에서 현재 위치 찾기
            int low = 1;
            int high = lineText.size() - startCol;
            int result = 0; // 기본값
            int pointX = pt.x - CalculateNumberAreaWidth()-m_margin.left;

            while (low <= high) {
                int mid = (low + high) / 2;
                if (mid <= 0) mid = 1; // 보호 코드

                int testSize = GetTextWidth(lineText.substr(startCol, mid));

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
        pos.lineIndex = m_scrollYLine + (pt.y + m_margin.top) / m_lineHeight;
        if (pos.lineIndex < 0) pos.lineIndex = 0;
        else if (pos.lineIndex >= (int)m_rope.getSize()) pos.lineIndex = (int)m_rope.getSize() - 1;
        // 수평 위치 계산
        std::wstring lineText = m_rope.getLine(pos.lineIndex);
        if (!lineText.empty()) {
            // 라인 번호 표시 중이면 여백만큼 좌측으로 이동
            int x = pt.x;
            if (m_showLineNumbers) {
                x -= CalculateNumberAreaWidth()+m_margin.left;
            }
            // 가로 스크롤 오프셋 적용
            x += m_scrollX;
            // 텍스트 폭 계산
            int col = 0;
            CSize extent;
            for (col = 0; col < (int)lineText.size(); ++col) {
                std::wstring text = lineText.substr(0, col + 1);
                extent = m_memDC.GetTextExtent(text.c_str(), text.length());
                if (extent.cx > x) break;
            }
            pos.column = col;
        }
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

// 캐럿이 보이도록 스크롤 조정 : 입력이나 이동이 있을 경우 호출, 캐럿이 보이지 않으면 스크롤 조정
void NemoEdit::EnsureCaretVisible() {
    CPoint pt = GetCaretPixelPos(m_caretPos);

    // 화면 표시 영역 크기 계산
    CRect client;
    GetClientRect(&client);
    int screenWidth = client.Width() - m_margin.left - m_margin.right - CalculateNumberAreaWidth();
    int screenHeight = client.Height() - m_margin.top - m_margin.bottom;
    int visibleLines = screenHeight / m_lineHeight; // 마진을 제외한 화면에 보이는 줄 수 ( top margin만 계산 )

    // 워드랩 모드
    if (m_wordWrap) {
        // 현재 라인의 몇 번째 워드랩 라인인지 계산
        int wrapLineIndex = 0; // 캐럿 위치가 속한 워드랩 라인 인덱스
        int startCol = 0; // 현재 워드랩 라인의 시작 컬럼
        std::vector<int> wrapPositions = FindWordWrapPosition(m_caretPos.lineIndex);
        for (size_t i = 0; i < wrapPositions.size(); i++) {
            if (m_caretPos.column < wrapPositions[i]) {
                break;
            }
            startCol = wrapPositions[i];
            wrapLineIndex++;
        }

        // 캐럿이 화면 위로 벗어남
        if (pt.y < 0) {
            m_scrollYLine = m_caretPos.lineIndex;
            m_scrollYWrapLine = wrapLineIndex;
            pt.y = 0;
        }
        // 캐럿이 화면 아래로 벗어남 : 현재의 캐럿 위치에서 위로 화면 줄수에 맞는 시작점을 구함
        else if (pt.y + m_lineHeight > screenHeight) {
            int totalLineCnt = 0;
            int wrapCnt = 0;
            totalLineCnt -= wrapPositions.size() - wrapLineIndex;
            for (int i = m_caretPos.lineIndex; i > 0; i--) {
                wrapCnt = FindWordWrapPosition(i).size() + 1; // 화면 줄수 + 워드랩 줄수
                totalLineCnt += wrapCnt;
                m_scrollYLine = i;
                m_scrollYWrapLine = 0;
                if (totalLineCnt >= visibleLines) {
                    // 초과된 라인 : 전체라인 - 화면라인수
                    // 마지막 라인의 워드랩 수 : 마지막 라인의 워드랩 라인수 - 초과된 라인수
                    m_scrollYWrapLine = totalLineCnt - visibleLines;
                    break;
                }
            }
            pt.y = (visibleLines - 1) * m_lineHeight;
        }

        // 캐럿이 안보일 경우에 스크롤 재계산 ( 워드랩이라 초과는 계산하지 않는다 )
		if (pt.x < 0) {
			m_scrollX = max(0, m_scrollX-pt.x);
			pt.x = 0;
		}
    }
    // 일반 모드
    else {
        int inc;

        // 기존 워드랩 비활성화 모드 (기존 코드 유지)
        if (pt.x < 0) {
            m_scrollX = max(0, m_scrollX + pt.x);
            pt.x = 0;
        }
        else if (pt.x + m_margin.right > screenWidth) {
            inc = pt.x + m_margin.right - screenWidth;
            m_scrollX += inc;
            pt.x -= inc;
        }

        // pt.y가 client.Height()보다 클 경우 스크롤이 필요함
        if (pt.y < 0) {
            m_scrollYLine = m_caretPos.lineIndex;
            pt.y = 0;
        }
        else if (pt.y + m_lineHeight >= screenHeight) {
            inc = pt.y + m_lineHeight - screenHeight;
            m_scrollYLine += inc / m_lineHeight + 1;
            inc = pt.y + m_lineHeight - (int)(screenHeight / m_lineHeight) * m_lineHeight;
            pt.y -= m_lineHeight;
        }
    }

    m_scrollYLine = max(0, m_scrollYLine);
    pt.x += CalculateNumberAreaWidth() + m_margin.left;
    pt.y += m_margin.top;

    NemoSetScrollPos(SB_HORZ, m_scrollX, TRUE);
    NemoSetScrollPos(SB_VERT, m_scrollYLine, TRUE);
    ::SetCaretPos(pt.x, pt.y);
    if (m_caretVisible == false) {
        ::ShowCaret(m_hWnd);
        m_caretVisible = true;
    }
    RecalcScrollSizes();
}

// 스크롤바 범위/페이지 크기 재계산
void NemoEdit::RecalcScrollSizes() {
    if (!m_hWnd) return;
	if (m_isUseScrollCtrl && m_showScrollBars == FALSE) return; //스크롤바 표시 X

    CRect client;
    GetClientRect(&client);
    CClientDC dc(this);
    dc.SelectObject(&m_font);

    // 라인 번호 영역 너비 계산
    int numberAreaWidth = 0;
    if (m_showLineNumbers) {
        numberAreaWidth = CalculateNumberAreaWidth();
    }

    if (m_wordWrap)
        m_wordWrapWidth = client.Width() - numberAreaWidth - m_margin.right - m_margin.left;

    // 스크롤 정보 구조체 초기화
    SCROLLINFO si;
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;

    if (m_wordWrap) {
        // 워드랩 모드에서는 수평 스크롤바를 비활성화
        m_scrollX = 0;

        int visibleLines = max(1, (client.Height()-m_margin.top-m_margin.bottom) / m_lineHeight);

        // 총 라인 수 - 전체 라인 기준으로 사용
        int totalRealLines = max(1, m_rope.getSize());

        // 수직 스크롤바 설정 - 일반 라인 수 기준으로 설정
        si.nMin = 0;
        si.nMax = totalRealLines + visibleLines/2 - 2;
        si.nPage = visibleLines;
        si.nPos = m_scrollYLine;
        NemoSetScrollInfo(SB_VERT, &si, TRUE);
    }
    else {
        // 일반 텍스트 라인 수 계산
        int totalDisplayLines = max(1,m_rope.getSize());

        // 최대 텍스트 폭 계산 (가로 스크롤)
        int maxWidth = GetMaxWidth();

        // 수직 스크롤바 설정
        si.nMin = 0;
        si.nMax = totalDisplayLines - 1;
        si.nPage = client.Height() / m_lineHeight;  // 한 페이지에 표시되는 라인 수
        si.nPos = m_scrollYLine;
        NemoSetScrollInfo(SB_VERT, &si, TRUE);

        // 수평 스크롤바 설정
        si.nMin = 0;
        si.nMax = (maxWidth > 0 ? maxWidth - 1 : 0);
        si.nPage = client.Width();
        si.nPos = m_scrollX;
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

    // 라인번호 폰트 설정
    LOGFONT lf;
    m_font.GetLogFont(&lf);
    lf.lfQuality = PROOF_QUALITY;  // 선명한 글꼴 렌더링

    // 현재 폰트와 색상 저장
    COLORREF oldTextColor = m_memDC.GetTextColor();
    CFont* pOldFont = m_memDC.GetCurrentFont();

    // 라인 번호용 새 폰트 설정
    LOGFONT lf_num;
    lf_num = lf;
    lf_num.lfWeight = FW_BOLD;
    CFont numFont;
    numFont.CreateFontIndirect(&lf_num);

    // 라인 번호 색상과 폰트 설정
    m_memDC.SetTextColor(m_colorInfo.lineNum);
    m_memDC.SelectObject(&numFont);

    // 라인 번호 그리기
    int numAreaWidth = CalculateNumberAreaWidth();
    CString numStr;
    CSize numSize;
    numStr.Format(_T("%d"), lineIndex + 1);
    numSize = m_memDC.GetTextExtent(numStr);
    int xPos = numAreaWidth - numSize.cx - 10;
    m_memDC.TextOut(xPos, yPos, numStr);

    // 원래 색상과 폰트로 복원
    m_memDC.SetTextColor(oldTextColor);
    m_memDC.SelectObject(pOldFont);

    // 임시 폰트 객체 정리
    numFont.DeleteObject(); // 사용이 끝난 CFont 객체 정리
}

// 화면 그리기 (더블 버퍼링 사용)
void NemoEdit::OnPaint() {
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);

    // 오프스크린 비트맵 크기 갱신
    if (client.Size() != m_memSize) {
        m_memBitmap.DeleteObject();
        m_memBitmap.CreateCompatibleBitmap(&dc, max(1, client.Width()), max(1, client.Height()));
        m_memDC.SelectObject(&m_memBitmap);
        m_memSize = client.Size();
    }

    // 배경 색으로 채우기
    m_memDC.FillSolidRect(client, m_colorInfo.textBg);
    m_memDC.SelectObject(&m_font);
    m_memDC.SetBkMode(TRANSPARENT);
    m_memDC.SetTextColor(m_colorInfo.text);

    // 라인 번호 영역 그리기
    int numberAreaWidth = 0;
    if (m_showLineNumbers) {
        numberAreaWidth = CalculateNumberAreaWidth();
        CRect gutterRect = client;
        gutterRect.right = numberAreaWidth;
        m_memDC.FillSolidRect(gutterRect, m_colorInfo.lineNumBg);
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
                    startPos = wrapPositions[i];
					continue;
				}
                // 화면 밖으로 나가면 중단
                if (y >= client.Height()) break;

                // 워드랩 라인이 보이는 영역에 있는지 확인
                if (lineIndex >= m_scrollYLine) {
                    int endPos;
					if (i == wrapPositions.size()) {
						// 마지막 라인 처리
						endPos = lineStr.length();
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
        int maxLine = m_rope.getSize();
        std::wstring lineStr;

        while ( lineIndex< maxLine && y < client.Height()) {
            lineStr = m_rope.getLine(lineIndex);
            // 수평 클리핑 최적화 (화면 밖에 있는 텍스트는 그리지 않음)
            int lineWidth = GetLineWidth(lineIndex);
            if (numberAreaWidth - m_scrollX + lineWidth <= 0) {
                // 완전히 화면 왼쪽 바깥에 있으면 그리지 않고 다음 라인으로
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
    dc.BitBlt(0, 0, client.Width(), client.Height(), &m_memDC, 0, 0, SRCCOPY);
}

BOOL NemoEdit::OnEraseBkgnd(CDC* pDC) {
    return FALSE; // 기본 배경 지우기 방지 (더블 버퍼링으로 그릴 것이므로)
}

// 주어진 텍스트를 화면에 출력 (선택 강조 포함)
// lineIndex: 라인 번호
 //segStartIdx: 컬럼 위치 ( wordwrap 이 있을 경우 현재 몇번째 컬럼부터 워드랩 된 것인지 표기 )
// segment: 출력할 텍스트 ( 워드랩인 경우 segStartIdx가 0이 아니면 잘린뒤의 텍스트 )
// xOffset: X 좌표
// y: Y 좌표
void NemoEdit::DrawSegment(int lineIndex, size_t segStartIdx, const std::wstring& segment, int xOffset, int y) {
    if (segment.empty()) {
        // 내용이 없는 경우도 캐럿 표시 위해 배경색으로 칠하기
        CRect rect(xOffset - m_scrollX, y, xOffset - m_scrollX + 2, y + m_lineHeight);
        m_memDC.FillSolidRect(rect, m_colorInfo.textBg);
        return;
    }

	std::wstring segText = segment;

    CRect client;
    GetClientRect(&client);

    // 수직 클리핑 (보이지 않는 라인은 건너뛰기)
    if (y + m_lineHeight <= 0 || y >= client.Height()) {
        return; // 보이지 않는 영역은 출력 생략
    }

    int x = xOffset - m_scrollX+m_margin.left;

    // 텍스트의 전체 너비 계산
    CSize textSize = m_memDC.GetTextExtent(segText.c_str(), segText.length());

    // 수평 클리핑 (화면 밖에 있는 텍스트는 그리지 않음)
    if (x + textSize.cx <= 0 || x >= client.Width()) {
        return; // 완전히 화면 밖에 있으면 그리지 않음
    }

    // 이 세그먼트 내 선택 영역 계산
    bool hasSelection = m_selectInfo.isSelected;
    size_t selStartCol = 0, selEndCol = 0;

    if (hasSelection) {
        // 선택 영역 정규화
        TextPos s = m_selectInfo.start;
        TextPos e = m_selectInfo.end;

        // 선택 영역이 뒤집혔으면 정렬
        if (e.lineIndex < s.lineIndex || (e.lineIndex == s.lineIndex && e.column < s.column)) {
            s = m_selectInfo.end;
            e = m_selectInfo.start;
        }

        // 텍스트에 선택 영역 표시
        if (lineIndex >= s.lineIndex && lineIndex <= e.lineIndex) {
            // 한라인에만 선택 영역이 존재할 경우
            if (s.lineIndex == e.lineIndex) {
                selStartCol = s.column;
                selEndCol = e.column;
            }
            // 현재 라인이 선택 영역의 시작인 경우
            else if (lineIndex == s.lineIndex) {
                selStartCol = s.column;
                selEndCol = segText.size()+segStartIdx;
            }
            else if (lineIndex == e.lineIndex) {
                selStartCol = 0;
                selEndCol = e.column;
            }
            else {
                selStartCol = 0;
                selEndCol = segText.size()+segStartIdx;
            }
        }
    }

    // 클리핑 영역 설정 - 보이는 영역으로 제한
    CRect clipRect(max(xOffset, x), y, min(client.Width(), x + textSize.cx), y + m_lineHeight);

    if (hasSelection && selEndCol > selStartCol) {
        // 텍스트와 겹치는 선택 범위 구하기
        size_t segEndIdx = segStartIdx + segText.size();
        if (selStartCol >= segEndIdx) {
            // 선택 부분이 이 세그먼트에 겹치지 않음 - 최적화된 그리기
            UINT textOutOptions = ETO_CLIPPED;
            m_memDC.ExtTextOut(x, y, textOutOptions, &clipRect,
                segText.c_str(), segText.size(), NULL);
            return;
        }

        // 세그먼트와 겹치는 선택 범위 구하기
        size_t drawSelStart = max(segStartIdx, selStartCol); // 선택 시작 위치
        size_t drawSelEnd = min(segStartIdx + segText.size(), selEndCol); // 선택 끝 위치 (현재 세그먼트 범위 내로 제한)

        // 선택 범위가 유효한지 확인 (끝이 시작보다 뒤에 있는지)
        if (drawSelEnd < drawSelStart) {
            drawSelEnd = drawSelStart;
        }

        size_t relSelStart = drawSelStart - segStartIdx; // 세그먼트 내 선택 시작 위치
        size_t relSelEnd = drawSelEnd - segStartIdx; // 세그먼트 내 선택 끝 위치

        // 선택 전 부분 출력 (클리핑 처리)
        if (relSelStart > 0) {
            std::wstring preText = segText.substr(0, relSelStart);
            CSize preSize = m_memDC.GetTextExtent(preText.c_str(), preText.length());

            if (x + preSize.cx > 0 && x < client.Width()) {
                m_memDC.ExtTextOut(x, y, ETO_CLIPPED, &clipRect,
                    preText.c_str(), preText.length(), NULL);
            }
            x += preSize.cx;
        }

        // 선택된 부분 출력 (클리핑 처리)
        if (relSelEnd > relSelStart) {
            std::wstring selText = segText.substr(relSelStart, relSelEnd - relSelStart);
            CSize selSize = m_memDC.GetTextExtent(selText.c_str(), selText.length());

            if (x + selSize.cx > 0 && x < client.Width()) {
                COLORREF oldTextColor = m_memDC.GetTextColor();
                CRect selRect(x, y, x + selSize.cx, y + m_lineHeight);

                // 선택 영역의 클리핑 처리
                CRect visibleSelRect = selRect;
                visibleSelRect.left = max(xOffset, visibleSelRect.left);
                visibleSelRect.right = min(client.Width(), visibleSelRect.right);

                if (!visibleSelRect.IsRectEmpty()) {
                    m_memDC.FillSolidRect(visibleSelRect, ::GetSysColor(COLOR_HIGHLIGHT));
                    m_memDC.SetTextColor(::GetSysColor(COLOR_HIGHLIGHTTEXT));
                    m_memDC.ExtTextOut(x, y, ETO_CLIPPED, &visibleSelRect,
                        selText.c_str(), selText.length(), NULL);
                    m_memDC.SetTextColor(oldTextColor);
                }
            }
            x += selSize.cx;
        }

        // 선택 후 남은 부분 출력 (클리핑 처리)
        if (relSelEnd < segText.size()) {
            std::wstring postText = segText.substr(relSelEnd);
            CSize postSize = m_memDC.GetTextExtent(postText.c_str(), postText.length());

            if (x < client.Width() && x + postSize.cx > 0) {
                CRect postClipRect(max(0, x), y, min(client.Width(), x + postSize.cx), y + m_lineHeight);
                m_memDC.SetBkColor(m_colorInfo.textBg);
                m_memDC.ExtTextOut(x, y, ETO_CLIPPED, &clipRect,
                    postText.c_str(), postText.length(), NULL);
            }
        }
    }
    else {
        // 선택 없는 경우 - 클리핑된 영역만 효율적으로 그리기
        UINT textOutOptions = ETO_CLIPPED;
        m_memDC.SetBkColor(m_colorInfo.textBg);
        m_memDC.ExtTextOut(x, y, textOutOptions, &clipRect,
            segText.c_str(), segText.length(), NULL);
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

// 윈도우 크기 조정
void NemoEdit::OnSize(UINT nType, int cx, int cy) {
    CWnd::OnSize(nType, cx, cy);
    if (cx <= 0 || cy <= 0) return;

    // 스크롤 크기 재계산
    RecalcScrollSizes();

    // 메모리 DC 비트맵 재설정
    CClientDC dc(this);

    // 새 비트맵 생성 및 선택
    m_memBitmap.DeleteObject();
    m_memBitmap.CreateCompatibleBitmap(&dc, cx, cy);
    m_memDC.SelectObject(&m_memBitmap);

    // 메모리 크기 갱신
    m_memSize = CSize(cx, cy);

    // 화면 갱신 요청
    Invalidate(FALSE);
}

// 수직 스크롤 이벤트 처리
void NemoEdit::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) {
    CRect client;
    GetClientRect(&client);
    int visibleLines = client.Height() / m_lineHeight;
    if(visibleLines < 1) visibleLines = 1;

    int totalLines = GetScrollLimit(SB_VERT) + 1;

    switch(nSBCode) {
        case SB_LINEUP: m_scrollYLine = max(0, m_scrollYLine - 1); break;
        case SB_LINEDOWN: m_scrollYLine = min(totalLines - 1, m_scrollYLine + 1); break;
        case SB_PAGEUP: m_scrollYLine = max(0, m_scrollYLine - visibleLines); break;
        case SB_PAGEDOWN: m_scrollYLine = min(totalLines - 1, m_scrollYLine + visibleLines); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si = { 0 };
            si.cbSize = sizeof(SCROLLINFO);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(SB_VERT, &si); // 다른 방법
			m_scrollYLine = si.nTrackPos;
        }
        break;
        default: break;
    }
	if (m_wordWrap) m_scrollYWrapLine = 0;

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
BOOL NemoEdit::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt) {
    int linesToScroll = 3;
    if(zDelta > 0) {
        m_scrollYLine = max(0, m_scrollYLine - linesToScroll);
    } else if(zDelta < 0) {
		// 스크롤바 사용하지 않을 경우 Wheel Scroll 처리
        if( m_isUseScrollCtrl && !m_showScrollBars ) m_scrollYLine = m_scrollYLine + linesToScroll;
		else m_scrollYLine = min(GetScrollLimit(SB_VERT), m_scrollYLine + linesToScroll);
    }
    if (m_wordWrap) m_scrollYWrapLine = 0;

    NemoSetScrollPos(SB_VERT, m_scrollYLine, TRUE);
	UpdateCaretPosition();
    Invalidate(FALSE);
    return TRUE;
}

void NemoEdit::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    // 포커스 설정 및 기본 더블클릭 처리
    SetFocus();

	TextPos pos = GetTextPosFromPoint(point);

    if (pos.lineIndex>=m_rope.getSize()) {
        CWnd::OnLButtonDblClk(nFlags, point);
        return;
    }

    std::wstring& lineText = m_rope.getLine(pos.lineIndex);
    // 선택할 단어의 시작과 끝 인덱스 찾기
    int wordStart=pos.column;
    int wordEnd=pos.column;
	while (wordStart > 0 && lineText[wordStart - 1]!=' ') {
		wordStart--;
	}
	while (wordEnd < (int)lineText.size() && lineText[wordEnd] != ' ') {
		wordEnd++;
	}
    // 선택된 내용이 없으면 중단
	if (wordStart == wordEnd) 
        return;

    // 선택 영역 설정 (단어 끝은 포함해야 하므로 +1)
    m_selectInfo.start.lineIndex = pos.lineIndex;
    m_selectInfo.start.column = wordStart;
    m_selectInfo.end.lineIndex = pos.lineIndex;
    m_selectInfo.end.column = wordEnd;
    m_selectInfo.isSelected = true;  // 단어가 선택되었으므로 true로 설정

    // 캐럿은 단어의 끝에 위치
    m_caretPos.lineIndex = pos.lineIndex;
    m_caretPos.column = wordEnd;
    m_selectInfo.anchor = m_selectInfo.start;

    UpdateCaretPosition();
    Invalidate(FALSE);

    CWnd::OnLButtonDblClk(nFlags, point);
}

// 마우스 왼쪽 버튼 눌렀을 때 (캐럿 이동 및 선택 시작)
void NemoEdit::OnLButtonDown(UINT nFlags, CPoint point) {
    SetFocus();

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
    // 캐럿 표시
    //CreateSolidCaret(2, m_lineHeight - m_lineSpacing);
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
            // 기존 일반 모드 코드 유지 (최소한의 변경)
            // 라인 번호 영역 처리
            int numberAreaWidth = 0;
            if (m_showLineNumbers) {
                numberAreaWidth = CalculateNumberAreaWidth();
                if (point.x < numberAreaWidth) point.x = numberAreaWidth;
            }

            // 라인 위치 계산
            int lineOffset = point.y / m_lineHeight;
            int newLineIndex = m_scrollYLine + lineOffset;
            if (newLineIndex < 0) newLineIndex = 0;
            if (newLineIndex >= (int)m_rope.getSize()) newLineIndex = (int)m_rope.getSize() - 1;

            // 컬럼 위치 계산
            int newColIndex = 0;
            if (newLineIndex < m_rope.getSize()) {
                std::wstring lineText = m_rope.getLine(newLineIndex);
                int textX = point.x + m_scrollX - numberAreaWidth;
                if (textX < 0) textX = 0;
                int low = 0, high = (int)lineText.size();
                while (low <= high) {
                    int mid = (low + high) / 2;
                    CSize sz = m_memDC.GetTextExtent(lineText.c_str(), mid);
                    if (sz.cx <= textX) {
                        newColIndex = mid;
                        low = mid + 1;
                    }
                    else {
                        high = mid - 1;
                    }
                }
            }

            // 캐럿 위치 업데이트
            if (newLineIndex != m_caretPos.lineIndex || newColIndex != m_caretPos.column) {
                TextPos oldCaret = m_caretPos;
                m_caretPos.lineIndex = newLineIndex;
                m_caretPos.column = newColIndex;

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
    if(nChar == '\t') {
        // Tab 입력: 4칸 공백 삽입
        InsertChar(L' ');
        InsertChar(L' ');
        InsertChar(L' ');
        InsertChar(L' ');
        EnsureCaretVisible();
        Invalidate(FALSE);
        return;
    }
    if(nChar == '\r' || nChar == '\n') {
        // Enter 입력: 새 줄 삽입
        if (m_selectInfo.isSelected) {
            DeleteSelection();
        }
        InsertNewLine();
    } else if(nChar >= 32) {
        // 일반 문자 입력
        if (m_selectInfo.isSelected) {
            DeleteSelection();
        }
        InsertChar((wchar_t)nChar);
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
            break; // 네비게이션 키 허용
        default:
            return; // 다른 키는 무시
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
        CRect client;
        GetClientRect(&client);
        int visibleLines = client.Height() / m_lineHeight;
        if(visibleLines < 1) visibleLines = 1;
        m_caretPos.lineIndex = max(0, m_caretPos.lineIndex - visibleLines);
        lineSize = m_rope.getLineSize(m_caretPos.lineIndex);
        int desiredCol = oldCaret.column;
        if(m_caretPos.lineIndex < m_rope.getSize() && desiredCol > lineSize) {
            desiredCol = lineSize;
        }
        m_caretPos.column = desiredCol;
        m_scrollYLine = max(0, m_scrollYLine - visibleLines);
        break;
    }
    case VK_NEXT: { // Page Down
        if (!shift && m_selectInfo.isSelected) {
            CancelSelection();
        }
        CRect client;
        GetClientRect(&client);
        int visibleLines = client.Height() / m_lineHeight;
        if(visibleLines < 1) visibleLines = 1;
        m_caretPos.lineIndex = min((int)m_rope.getSize() - 1, m_caretPos.lineIndex + visibleLines);
        lineSize = m_rope.getLineSize(m_caretPos.lineIndex);
        int desiredCol = oldCaret.column;
        if(m_caretPos.lineIndex < m_rope.getSize() && desiredCol > lineSize) {
            desiredCol = lineSize;
        }
        m_caretPos.column = desiredCol;
        m_scrollYLine = min(GetScrollLimit(SB_VERT), m_scrollYLine + visibleLines);
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
                return TRUE;
            }
            switch (nChar) {
            case 'C': case 'X': case 'V': case 'A': case 'Z': case 'Y':
                if (ctrl) {  // Ctrl + C/X/V/A/Z/Y 처리
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
					}
                    EnsureCaretVisible();
                    Invalidate(FALSE);
                    return TRUE;
                }
                break;
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
        int startPos = offset;
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

    int actualSize = size;
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
        if (lineCnt++ > 0) text += L"\n";
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
        text += L"\n";
        if (endLineIndex - startLineIndex > 1) {
            auto it = std::next(itStart);
            for (int line = startLineIndex + 1; line < endLineIndex; line++) {
                if (it == lines.end()) break;
                text += *it;
                text += L"\n";
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
    catch (const std::exception& e) {
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
        int eraseCnt = node->data.size();
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
    RopeNode* newRoot = buildBalancedTree(leaves, 0, leaves.size() - 1);
    //RopeNode* newRoot = buildBalancedTreeBottomUp(leaves);
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

void Rope::insertMultiple(size_t lineIndex, const std::deque<std::wstring>& newLines) {
    if (!root) return;
    bool isEnd = false;
    int insertIndex = lineIndex;
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
    std::list<std::wstring> tempLines;

    RopeNode* addLeaf = new RopeNode();
    addLeaf->isLeaf = true;
    addLeaf->length = 0;
    addLeaf->left = addLeaf->right = addLeaf->parent = nullptr;

    for (const auto& line : newLines) {
        // 새 라인을 실제 라인 컬렉션에 추가
        tempLines.push_back(line);
        auto lineIter = tempLines.end();
        --lineIter; // 마지막 요소의 반복자

        // 현재 리프 노드에 라인 추가
        addLeaf->data.push_back(lineIter);
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
        lines.splice(lines.end(), tempLines);
        leaves.splice(leaves.end(), newLeafNodes);
    }
    else {
        if (insertPos != lines.end()) lines.splice(insertPos, tempLines);
        else lines.splice(lines.end(), tempLines);

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
    RopeNode* newRoot = buildBalancedTree(leaves, 0, leaves.size() - 1);
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

int Rope::updateNodeLengths(RopeNode* node) {
    if (node == nullptr) return 0;

    // 리프 노드인 경우
    if (node->isLeaf) {
        node->length = node->data.size(); // 문자열 길이를 weight로 설정
        return node->length;
    }

    // 내부 노드: 좌우 자식의 Length를 재귀적으로 계산
    node->length = (node->left != nullptr) ? updateNodeLengths(node->left) : 0;
    int rightLength = (node->right != nullptr) ? updateNodeLengths(node->right) : 0;

    return node->length + rightLength;
}
