//﻿*******************************************************************************
//    파     일     명 : NemoEdit.h
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
#pragma once
//#ifdef _DEBUG
//#define _SECURE_SCL 0
//#define _HAS_ITERATOR_DEBUGGING 0
//#define _ITERATOR_DEBUG_LEVEL 0  
//#endif
#include <afxwin.h>
#include <list>
#include <string>
#include <vector>
#include <deque>
#include <imm.h>
#include <map>
#include <iostream>
#include <functional>
#include <optional>
#include <stack>
#include <algorithm>
#include <d2d1.h>
#include <dwrite.h>
#include <atlbase.h>

#define SPLIT_THRESHOLD         2000
#define MERGE_THRESHOLD     1000

#define CURSOR_UP 1
#define CURSOR_DOWN -1

using namespace std;

class RopeNode {
public:
    std::vector<std::list<std::wstring>::iterator> data;  // 리프 노드의 라인 이터레이터들
    size_t      length;  // 이 노드(서브트리)가 보유한 총 라인 수
    RopeNode* left;
    RopeNode* right;
    RopeNode* parent;
    bool        isLeaf;

    RopeNode() : length(0), left(nullptr), right(nullptr), parent(nullptr), isLeaf(true) { data.reserve(SPLIT_THRESHOLD + 1);  }
    ~RopeNode() { data.clear(); }
};

class Rope {
private:
    RopeNode* root;    // Rope 트리의 루트 노드
    size_t m_balanceCnt; // 트리 재조정용 체크 카운터

    // 내부 함수
    RopeNode* findLeaf(RopeNode* node, size_t idx, size_t& offset);
    void updateLengthUpward(RopeNode* node, int addCnt); // 현재 leafNode에서부터 root까지 parent만 타고가면서 length 변경사항 적용
    void splitNode(RopeNode* leaf);
    bool splitNodeByExact(RopeNode* leaf, size_t cutSize); // 중간삽입시 분리용
    void mergeIfNeeded(RopeNode* leaf);
    void deleteLeafNode(RopeNode* node);
    void deleteAllNodes(RopeNode* node);

    // 휴리스틱 최적화
    void balanceRope();
    void collectLeafNodes(RopeNode* node, std::list<RopeNode*>& leaves);
    void collectInternalNodes(RopeNode* node, std::vector<RopeNode*>& internals);
    RopeNode* buildBalancedTree(std::list<RopeNode*>& leaves, int start, int end);
    size_t updateNodeLengths(RopeNode* node); // root에 걸어서 length 재계산
    bool isUnbalanced();
    int getMaxDepth(RopeNode* node);
    int getMinDepth(RopeNode* node);

public:
    std::list<std::wstring> lines;  // 원본 텍스트 라인 저장소

    Rope();
    ~Rope();
    // 핵심 연산들:
    void insert(size_t lineIndex, const std::wstring& text);
    void insertAt(size_t lineIndex, size_t offset, const std::wstring& text);
    void insertBack(const std::wstring& text);
    void insertMultiple(size_t lineIndex, std::list<std::wstring>& newLines);
    void erase(size_t lineIndex);
    void eraseAt(size_t lineIndex, size_t offset, size_t size);
    void eraseRange(size_t startLine, size_t endLine);
    void update(size_t lineIndex, const std::wstring& newText);
    void mergeLine(size_t lineIndex);
    bool clear(); // 전체 초기화
    bool empty(); // lines.empty()
    std::list<std::wstring>::iterator getIterator(size_t lineIndex); // lines의 해당 라인 it
    std::list<std::wstring>::iterator getEnd(); // lines의 end
    std::list<std::wstring>::iterator getBegin(); // lines의 begin
    size_t getSize(); // 전체 줄수
    size_t getLineSize(size_t lineIndex); // 라인 사이즈
    std::wstring getLine(size_t lineIndex); // 라인 텍스트
    std::wstring getText(); // 전체 텍스트
    std::wstring getTextRange(size_t startLineIndex, size_t startLineColum, size_t endLineIndex, size_t endLineColumn); // 구간 텍스트
};

// TextMetrics 구조체 정의
struct TextMetrics {
    float ascent;                    // 기준선에서 문자의 최상단까지의 거리
    float descent;                   // 기준선에서 문자의 최하단까지의 거리
    float lineGap;                   // 줄 간 여백 크기
    float capHeight;                 // 대문자의 높이
    float xHeight;                   // 소문자 'x'의 높이 (소문자 높이 기준)
    float underlinePosition;         // 밑줄의 세로 위치
    float underlineThickness;        // 밑줄의 두께
    float strikethroughPosition;     // 취소선의 세로 위치
    float strikethroughThickness;    // 취소선의 두께
    float lineHeight;                // 한 줄의 전체 높이
};

// D2Render 클래스 정의
class D2Render {
public:
    D2Render();                      // 생성자: 기본값으로 객체 초기화
    ~D2Render();                     // 소멸자: 리소스 해제

    // 초기화 및 리소스 관리
    bool Initialize(HWND hwnd);      // D2D/DWrite 초기화 및 창 핸들과 연결
    void SetScreenSize(int width, int height);  // 렌더링 영역 크기 설정
    void Clear(COLORREF bgColor);    // 배경색으로 화면 지우기
    void BeginDraw();                // 그리기 작업 시작
    void EndDraw();                  // 그리기 작업 종료 및 화면 업데이트
    void Resize(int width, int height);  // 창 크기 변경 시 렌더 타겟 크기 조정
    void Shutdown();                 // 모든 D2D/DWrite 리소스 해제

    // 폰트 및 텍스트 설정
    void SetFont(std::wstring fontName, int fontSize, bool bold, bool italic);  // 폰트 설정
    void SetFontSize(int size);  // 폰트 사이즈 설정
    void GetFont(std::wstring& fontName, int& fontSize, bool& bold, bool& italic);  // 현재 폰트 정보 가져오기
    int GetFontSize();  // 현재 폰트 사이즈 가져오기
    void SetSpacing(int spacing);  // 줄 간격 설정
    void SetTextColor(COLORREF textColor);     // 텍스트 색상 설정
    void SetBgColor(COLORREF bgColor);         // 배경 색상 설정
    void SetLineNumColor(COLORREF lineNumColor);  // 줄 번호 색상 설정
    void SetLineNumBgColor(COLORREF bgColor);  // 줄 번호 색상 설정
    void SetSelectionColors(COLORREF textColor, COLORREF bgColor);  // 선택 영역 색상 설정

    // 텍스트 측정 및 분석
    float GetTextWidth(const std::wstring& line);  // 텍스트 문자열의 픽셀 너비 계산
    std::vector<int> MeasureTextPositions(const std::wstring& text);  // 텍스트 내의 각 문자 위치(오프셋)를 픽셀 단위로 측정
    TextMetrics GetTextMetrics() const;  // 현재 폰트의 메트릭스(높이, 간격 등) 정보 반환
    float GetLineHeight() const;     // 현재 폰트의 줄 높이 반환

    // 텍스트 그리기
    void FillSolidRect(const D2D1_RECT_F& rect, COLORREF color);  // 단색으로 사각형 채우기
    void DrawEditText(float x, float y, const D2D1_RECT_F* clipRect, const wchar_t* text, size_t length);  // 텍스트 그리기 (선택 없음)
    void DrawEditText(float x, float y, const D2D1_RECT_F* clipRect, const wchar_t* text,
        size_t length, bool selected, int startSelectPos, int endSelectPos);  // 텍스트 그리기 (부분 선택 가능)
    void DrawLineText(float x, float y, const D2D1_RECT_F* clipRect, const wchar_t* text, size_t length);  // 줄 번호 텍스트 그리기

private:
    // DirectWrite 및 Direct2D 리소스
    CComPtr<ID2D1Factory> m_pD2DFactory;              // D2D 팩토리 인터페이스
    CComPtr<IDWriteFactory> m_pDWriteFactory;         // DirectWrite 팩토리 인터페이스
    CComPtr<ID2D1HwndRenderTarget> m_pRenderTarget;   // 창에 그리기 위한 렌더 타겟
    CComPtr<IDWriteTextFormat> m_pTextFormat;         // 일반 텍스트 포맷
    CComPtr<IDWriteTextFormat> m_pLineNumFormat;      // 줄 번호 텍스트 포맷
    CComPtr<ID2D1SolidColorBrush> m_pTextBrush;       // 일반 텍스트 브러시
    CComPtr<ID2D1SolidColorBrush> m_pBgBrush;         // 배경 브러시
    CComPtr<ID2D1SolidColorBrush> m_pSelectedTextBrush;  // 선택된 텍스트 브러시
    CComPtr<ID2D1SolidColorBrush> m_pSelectedBgBrush;    // 선택된 배경 브러시
    CComPtr<ID2D1SolidColorBrush> m_pLineNumBrush;       // 줄 번호 텍스트 브러시

    // 캐시된 텍스트 메트릭스
    TextMetrics m_textMetrics;       // 현재 폰트의 메트릭스 정보 저장

    // 폰트 설정
    std::wstring m_fontName;         // 폰트 이름 (예: "Consolas", "D2Coding")
    float m_fontSize;                // 폰트 크기 (포인트 단위)
    DWRITE_FONT_WEIGHT m_fontWeight; // 폰트 두께 (일반, 굵게 등)
    DWRITE_FONT_STYLE m_fontStyle;   // 폰트 스타일 (일반, 이탤릭 등)

    // 색상 설정
    D2D1::ColorF m_textColor;        // 텍스트 색상
    D2D1::ColorF m_bgColor;          // 배경 색상
    D2D1::ColorF m_selectedTextColor;  // 선택된 텍스트 색상
    D2D1::ColorF m_selectedBgColor;    // 선택된 배경 색상
    D2D1::ColorF m_lineNumColor;       // 줄 번호 색상
    D2D1::ColorF m_lineNumBgColor;     // 줄 번호 배경 색상

    // 창 크기
    int m_width;                     // 렌더링 영역 너비
    int m_height;                    // 렌더링 영역 높이
    int m_spacing;                  // 줄 간격 (기본값: 0)

    // 초기화 상태
    bool m_initialized;              // D2D/DWrite 초기화 완료 여부
    HWND m_hwnd;

    // 내부 메소드
    void UpdateTextMetrics();        // 폰트 변경 시 텍스트 메트릭스 정보 업데이트
    bool CreateTextFormat();         // 텍스트 포맷 객체 생성
    bool CreateBrushes();            // 브러시 객체 생성
public:
    ID2D1HwndRenderTarget* GetRenderTarget() const { return m_pRenderTarget; }
    void LogRenderTargetState();
};

struct Margin {
	int left;
	int right;
	int top;
	int bottom;
};

// IME 관련 구조체
struct IMECompositionInfo {
    bool isComposing = false;         // IME 조합 중 여부
    int lineNo = 0;                   // IME 조합 중인 라인 번호
    int startPos = 0;              // IME 조합 시작 위치
    std::wstring imeText;              // 현재 IME 조합 중인 텍스트
};

// 내부 자료구조와 기능
struct TextPos {
	int lineIndex; // 라인 인덱스 (0부터 시작)
	int column; // 열 인덱스 (0부터 시작)
    TextPos(int li = 0, int col = 0) : lineIndex(li), column(col) {}
};

struct SelectInfo {
	bool isSelected; // 선택된 상태인지 여부
	bool isSelecting = false; // 선택중이거나 선택된 상태인지 여부
    TextPos start;
    TextPos end;
	TextPos anchor;
};

struct ColorInfo {
	COLORREF text;
	COLORREF textBg;
	COLORREF lineNum;
	COLORREF lineNumBg;
	COLORREF select;
};

// MFC CWnd 기반 텍스트 에디터 컨트롤 NemoEdit 클래스
class NemoEdit : public CDialogEx {
public:
    NemoEdit();
    virtual ~NemoEdit();

    // 컨트롤 생성 함수
    BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);

    // 설정 메서드
    int GetLineHeight() { return m_lineHeight; }
	void SetFont(std::wstring fontName, int fontSize, bool bold, bool italic);
    void GetFont(std::wstring& fontName, int& fontSize, bool& bold, bool& italic);
    void ApplyFont();
    void SetFontSize(int size);
	int GetFontSize();
    void SetTabSize(int size);
    int GetTabSize();
    void SetLineSpacing(int spacing);
    void SetWordWrap(bool enable);
    void ShowLineNumbers(bool show);
	void SetReadOnly(bool isReadOnly);
	void SetMargin(int left, int right, int top, int bottom);
	void SetTextColor(COLORREF textColor, COLORREF bgColor);
    void SetTextColor(COLORREF textColor);
    void SetBgColor(COLORREF textBgColor, COLORREF lineBgColor);
    COLORREF GetTextColor();
    COLORREF GetTextBgColor();
	void SetLineNumColor(COLORREF lineNumColor, COLORREF bgColor);

    // 텍스트 조작 메서드
    void SetText(const std::wstring& text);
    std::wstring GetText();
	std::wstring GetSelect();
    void AddText(std::wstring text);
    void Copy();
    void Cut();
    void Paste();
    void Undo();
    void Redo();
    void UpDown(int step);
    void ActiveScrollCtrl(bool isUse);  // 스크롤바 컨트롤 실행/중지 함수
	void SetScrollCtrl(bool show);  // 스크롤바 표시 설정 함수 ( 스크롤바 컨트롤 사용시에만 동작한다. )
	void SelectAll();
	size_t GetSize();
	int GetCurrentLineNo();
    void GotoLine(size_t lineNo);

protected:

    struct UndoRecord {
        enum Type { Insert, Delete, Replace } type;

        // 작업 범위 (모두 작업 전 상태 기준)
        TextPos start;          // 작업 시작 위치 (작업 전)
        TextPos end;            // 작업 끝 위치 (작업 전, Delete/Replace용)
        std::wstring text;      // Insert: 삽입할 내용, Delete/Replace: 원본 내용

        // 선택 영역 복원용 (작업 전 상태)
        bool hadSelection;      // 작업 전에 선택 영역이 있었는지
        TextPos selectStart;    // 작업 전 선택 영역 시작
        TextPos selectEnd;      // 작업 전 선택 영역 끝
        TextPos caretPos;       // 작업 전 캐럿 위치

        UndoRecord() : type(Insert), hadSelection(false) {}
    };

    // 메시지 처리 함수들
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnSetFocus(CWnd* pOldWnd);
    afx_msg void OnKillFocus(CWnd* pNewWnd);
    afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    // IME 이벤트 핸들러
    afx_msg LRESULT OnImeStartComposition(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnImeComposition(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnImeChar(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnImeEndComposition(WPARAM wParam, LPARAM lParam);

    DECLARE_MESSAGE_MAP()

private:
    // 내부 기능 메서드들
    int GetLineWidth(int lineIndex);
    int GetMaxWidth(); // lineWidth의 최대값을 리턴한다.
    // 라인번호 표시 관련
	int CalculateNumberAreaWidth(); // 라인 번호 영역 너비 계산
	// 텍스트 조작 관련
    void InsertChar(wchar_t ch);
    void InsertNewLine();
	void DeleteChar(bool backspace);
    void CancelSelection();
	void DeleteSelection();
	void ReplaceSelection(std::wstring text);
    int GetTextWidth(const std::wstring& line); // 문자의 길이를 캐싱된 데이터로 계산
	std::vector<int> FindWordWrapPosition(int lineIndex); // 자동 줄바꿈 위치 찾기
	void SplitTextByNewlines(const std::wstring& text, std::vector<std::wstring>& parts); // 텍스트를 줄바꿈 문자로 분리
    void AddTabToSelectedLines();      // 여러 줄 선택 시 탭 추가 처리 메서드
    void RemoveTabFromSelectedLines(); // 여러 줄 선택 시 탭 제거 처리 메서드
	// 캐럿 관련
    CPoint GetCaretPixelPos(const TextPos& pos); // 캐럿 위치를 픽셀 좌표로 변환
    TextPos GetTextPosFromPoint(CPoint pt); // 마우스  클릭 위치를 텍스트 위치로 변환
	void UpdateCaretPosition(); // 캐럿 위치 갱신
	void EnsureCaretVisible(); // 캐럿이 보이도록 스크롤 조정
	// 스크롤 관련
	void RecalcScrollSizes(); // 스크롤 사이즈 재계산
    void NemoShowScrollBar(UINT nBar, BOOL bShow); // NemoShowScrollBar 래핑 함수
    void NemoSetScrollInfo(UINT nBar, LPSCROLLINFO lpScrollInfo,
		BOOL bRedraw); // NemoSetScrollInfo 래핑 함수
	void NemoSetScrollPos(int nBar, int nPos, BOOL bRedraw); // NemoSetScrollPos 래핑 함수
    // 텍스트 그리기
    //int GetLineWidth(int lineIndex);
    void DrawLineNo(int lineIndex, int yPos);
    void DrawSegment(int lineIndex, size_t segStartIdx, const std::wstring& segment, int xOffset, int y);
    // 이동
    void MoveCaretToPrevWord();  // 이전 단어의 시작으로 이동
    void MoveCaretToNextWord();  // 다음 단어의 시작으로 이동
	void SaveClipBoard(const std::wstring& text); // 클립보드에 텍스트 저장
	std::wstring LoadClipText(); // 클립보드에서 텍스트 로드
    void HideIME(); // IME 숨기기
    void ClearText();
    std::wstring ExpandTabs(const std::wstring& text); // \t을 space * tabSize로 치환
    int TabCount(const std::wstring& text, int endPos);
	void HandleTripleClick(CPoint point); // 트리플 클릭 처리
    // 단어 경계 검사
    void InitializeWordDelimiters();        // 구분자 초기화 함수
    bool IsWordDelimiter(wchar_t ch);      // 구분자 확인
    void FindWordBoundary(const std::wstring& text, int position, int& start, int& end);  // 단어 경계 찾기
    // Undo 관련 헬퍼 함수들
    void SaveCurrentState(UndoRecord& record);              // 현재 상태를 레코드에 저장
    void RestoreState(const UndoRecord& record);            // 레코드로부터 상태 복원
    void AddUndoRecord(const UndoRecord& record);           // Undo 스택에 레코드 추가
    UndoRecord CreateInsertRecord(const TextPos& pos, const std::wstring& text);
    UndoRecord CreateDeleteRecord(const TextPos& start, const TextPos& end, const std::wstring& text);
    UndoRecord CreateReplaceRecord(const TextPos& start, const TextPos& end, const std::wstring& originalText);
    void DeleteSelectionRange(const TextPos& start, const TextPos& end);
    void InsertTextAt(const TextPos& pos, const std::wstring& text);

    // 내부 데이터
	Rope m_rope; // 텍스트 데이터를 관리하는 Rope 객체

	D2Render m_d2Render;
    TextPos m_caretPos;                       // 캐럿 위치 (라인, 칼럼)
	bool m_caretVisible;                      // 캐럿 표시 여부
	SelectInfo m_selectInfo;                  // 선택 영역 정보
    IMECompositionInfo m_imeComposition; // IME 조합 정보 구조체
	// numberArea 관련
    mutable int m_nextDiffNum; // 다음 재계산될 라인 번호
    mutable int m_numberAreaWidth;        // 라인 번호 영역 너비

    // 설정 플래그 및 파라미터
	bool m_isReadOnly;            // 읽기 전용 여부
    bool m_wordWrap;              // 자동 줄바꿈 여부
    bool m_showLineNumbers;       // 라인 번호 표시 여부
    int m_wordWrapWidth;           // WordWrap : 한 줄의 최대 너비 (픽셀, 0이면 제한 없음)
    int m_lineSpacing;            // 추가 줄 간격 (픽셀)
    int m_tabSize; // 탭 사이즈 ( space bar width 기준 ) : space width*m_tabSize = 최종 tab width
    int m_maxWidth; // 현재 문서의 라인 최대 사이즈
	
    // 여백
	Margin       m_margin;              // 여백 : 오른쪽만 구현
    // 색상
	ColorInfo m_colorInfo;         // 색상 정보

    // 폰트와 렌더링 관련
	int m_lineHeight; // 한 라인의 높이
	int m_charWidth; // 라인번호 표시 폰트의 문자 폭

    // 트리플 클릭 관련 변수
    CPoint m_lastClickPos;     // 마지막 클릭 위치
    DWORD m_lastClickTime;     // 마지막 클릭 시간
    int m_clickCount;          // 클릭 카운트

    // 스크롤바
    BOOL m_isUseScrollCtrl; // 스크롤바 컨트롤 사용 여부 ( 이걸 사용하면 수직/수평 스크롤바의 표시를 제어할 수 있다. )
    BOOL m_showScrollBars;  // 스크롤바 표시 여부를 제어하는 단일 플래그

    // 스크롤 상태
    int m_scrollX; // 수평 스크롤 : 픽셀 단위로 스크린에서 제외된 크기 ( 0부터 시작 )
	int m_scrollYLine; // 수직 스크롤 : 스크린 첫라인 번호 ( 0부터 시작 )
	int m_scrollYWrapLine; // 수직 스크롤 : 스크린 첫라인 wordwrap 번호 ( 0이면 라인의 시작, 1이면 워드랩 첫줄 )

    // Undo/Redo 스택
    std::vector<UndoRecord> m_undoStack;
    std::vector<UndoRecord> m_redoStack;

    // 단어 경계 검사
    bool isDivChar[256];                   // 구분자 빠른 검색을 위한 배열
public:
    virtual BOOL PreTranslateMessage(MSG* pMsg);
};
