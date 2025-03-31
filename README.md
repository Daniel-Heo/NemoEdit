# 네모에디터(NemoEdit)의 특징
 네모에디터는 Windows 환경에서 한글 지원과 대용량 텍스트 처리에 중점을 둔 특화된 에디터입니다. 고급 기능보다는 성능과 한글 입력에 최적화되어 있으며, MFC 기반 애플리케이션에 쉽게 통합할 수 있습니다. 다른 범용 에디터 컴포넌트들에 비해 기능은 제한적이지만, 특정 요구사항(한글 지원, 대용량 처리)에 맞게 최적화되어 있습니다.
 대략 파일 2G에 16G의 메모리를 사용합니다. 메모리 사용량이 많은 이유는 1억줄 정도에 list와 wstring의 메모리 때문입니다. 메모리를 절약하기위해서는 list와 wstring을 버리고 가벼운 단방향 링크드리스트와 char나 wchar_t로 관리하는 구조로 변경해야되는데 너무 변경사항이 많아서 필요할 경우에 고려해보겠습니다. 그렇게 구현하면 메모리풀도 적용해서 성능을 더 끌어올릴 수 있을 것 같습니다.

## 장점

- 한글 지원에 최적화: 코드에서 명시된 것처럼 한글 지원에 중점을 둔 에디터
- Rope 자료구조 사용: 대용량 텍스트 처리에 효율적인 Rope 자료구조 채택
- 빠른 성능: 대용량 텍스트 처리와 렌더링 최적화(더블 버퍼링 등) 적용
- IME 지원: 한국어, 중국어, 일본어 등 IME 입력 방식 지원
- MFC 기반: Windows 애플리케이션에 쉽게 통합 가능
- 경량 설계: 최소한의 기능으로 가볍게 설계됨
- 워드랩 기능: 자동 줄바꿈 지원으로 가독성 향상

## 단점

- 플랫폼 한정: MFC 기반이므로 Windows 플랫폼에서만 사용 가능
- 기능 제한: 구문 강조, 자동 완성 등 고급 기능 부재
- 확장성 제약: 플러그인 시스템이 없어 확장이 제한적
- 리소스 사용: MFC 사용으로 인한 추가 리소스 요구

# 사용법
NemoEdit.h, NemoEdit.cpp 파일을 프로젝트에 추가하고 아래 내용대로 설정한다.
```
View 클래스 헤더에 NemoEdit.h 추가, m_editCtrl 멤버변수 추가
// ------------------------
#include "NemoEdit.h"
NemoEdit m_editCtrl;

View 클래스 OnCreate에 아래 내용을 넣는다. 기본적으로 Create만 사용하면 빈 에디터가 생성된다.
// ------------------------
// NemoEdit 컨트롤 생성
CRect editRect(10, 10, 500, 400);  // 크기 설정
m_editCtrl.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, editRect, this, 1001);
```

추가 설정 및 사용
```
// 기본 텍스트 설정
std::wstring text = L"이것은 NemoEdit의 예제입니다.\nMFC 기반의 텍스트 에디터 컨트롤입니다.\n";
m_editCtrl.SetText(text);

// 새 텍스트 설정
m_editCtrl.SetText(L"");

// 텍스트 추가
m_editCtrl.AddText(L"이것은 추가된 텍스트입니다.\n");

// 에디터 텍스트 가져오기
text = m_editCtrl.GetText();

// 폰트 설정 : 일부 폰트에서 한글과 영문이 섞일 경우 slect에서 영역이 좁아지는 현상이 있습니다.
//           폰트 사이즈를 12, 16을 사용하면 해결되는 경우도 있습니다.
m_editCtrl.SetFont(L"Arial", 16, true, false); // 글꼴, 크기, 볼드, 이탤릭
// 라인 여백 설정
m_editCtrl.SetLineSpacing(5); // 5픽셀 추가 여백
// 워드랩 설정
m_editCtrl.SetWordWrap(true);
// 라인 번호 표시
m_editCtrl.ShowLineNumbers(true);
// 읽기 전용 설정
m_editCtrl.SetReadOnly(true);
// 여백 설정
m_editCtrl.SetMargin(5, 20, 5, 0); // 왼쪽, 오른쪽, 위, 아래 (오른쪽은 한글 입력 IME문자폭+여백)
// 텍스트 색상 설정
m_editCtrl.SetTextColor(RGB(180, 180, 200), RGB(28, 29, 22)); // 텍스트 색상, 배경색
// 라인 번호 색상 설정
m_editCtrl.SetLineNumColor(RGB(140, 140, 140), RGB(28, 29, 22)); // 라인 번호 색상, 배경색
// 스크롤바 컨트롤 사용
m_editCtrl.ActiveScrollCtrl(true); // false일 경우에 스크롤바 컨트롤 사용 안함
// 스크롤바 표시 설정
m_editCtrl.SetScrollCtrl(false); // false일 경우에 스크롤바 표시 안함
```

# 라이센스 ( License )
듀얼 라이센스
1. 대한민국 시민권자가 아닌 경우 : AGPL 3.0 License
2. 대한민국 시민권자인 경우 : MIT License

Dual License
1. If you are not a citizen of the Republic of Korea: AGPL 3.0 License
2. If you are a citizen of the Republic of Korea: MIT License
   


