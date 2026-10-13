// FFGL and some Windows build environments use reduced Windows headers.
// GDI+ needs the complete GDI/metafile declarations, so explicitly restore
// them before any Windows or FFGL header is loaded.
#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif
#ifdef NOMETAFILE
#undef NOMETAFILE
#endif
#ifdef NOGDI
#undef NOGDI
#endif
#include <windows.h>
#include <wingdi.h>
#include <objidl.h>
#include <ole2.h>
#include <gdiplus.h>

#include "BRRunningText.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

using namespace Gdiplus;
using namespace ffglex;

namespace {
enum Param : FFUInt32 {
  P_START, P_PAUSE, P_RESUME, P_END, P_PLAYBACK_STYLE,
  P_MOTION, P_REPEAT_MODE, P_SEPARATOR, P_SPEED, P_LOOP_GAP, P_EDGE_DELAY, P_RESET,
  P_EDIT_TEXT, P_TEXT, P_FONT_NAME, P_FONT_LIST, P_REFRESH_FONTS,
  P_FONT_SIZE, P_ALIGN, P_LETTER_SPACING, P_LINE_SPACING,
  P_TEXT_H, P_TEXT_S, P_TEXT_B, P_TEXT_A,
  P_TEXT_STROKE, P_TEXT_STROKE_WIDTH, P_TEXT_STROKE_SCALE, P_TEXT_STROKE_H, P_TEXT_STROKE_S,
  P_TEXT_STROKE_B, P_TEXT_STROKE_A,
  P_BOX_MODE, P_BOX_WIDTH, P_BOX_HEIGHT, P_PAD_X, P_PAD_Y,
  P_BOX_H, P_BOX_S, P_BOX_B, P_BOX_A, P_BOX_OPACITY, P_RADIUS,
  P_BOX_STROKE, P_BOX_STROKE_WIDTH, P_BOX_STROKE_H, P_BOX_STROKE_S,
  P_BOX_STROKE_B, P_BOX_STROKE_A, P_BOX_STROKE_POSITION,
  P_BOX_STROKE_OFFSET_X, P_BOX_STROKE_OFFSET_Y,
  P_POS_X, P_POS_Y, P_SCALE, P_ROTATION,
  P_FLICKER, P_FLICKER_SPEED, P_FLICKER_AMOUNT, P_FLICKER_MIN,
  P_WIGGLE, P_WIGGLE_X, P_WIGGLE_Y, P_WIGGLE_ROTATION, P_WIGGLE_SPEED,
  P_ANIM_IN, P_IN_AXIS, P_IN_INVERT, P_IN_DISTANCE, P_IN_DURATION, P_IN_DELAY, P_IN_EASE,
  P_ANIM_OUT, P_OUT_AXIS, P_OUT_INVERT, P_OUT_DISTANCE, P_OUT_DURATION,
  P_OUT_DELAY, P_OUT_EASE, P_OUT_TRIGGER, P_HOLD_DURATION, P_PLAY_OUT,
  P_CREDIT,
  P_COUNT
};

static CFFGLPluginInfo PluginInfo(
  PluginFactory<BRRunningText>, "BRR5", "BR Running Text",
  2, 1, 0, 401, FF_SOURCE,
  "Running text, credits and lower thirds by BELAJAR RESOLUME",
  "Effect by Belajar Resolume");

float brClamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

std::wstring utf8ToWide(const std::string& value) {
  if (value.empty()) return L"";
  int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
  if (count <= 0) return std::wstring(value.begin(), value.end());
  std::wstring out(static_cast<size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), count);
  out.pop_back();
  return out;
}

std::string expandLineBreaks(std::string value) {
  size_t position=0;
  while((position=value.find("\\n",position))!=std::string::npos){value.replace(position,2,"\n");++position;}
  return value;
}

std::string wideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  int count = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(count), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), count, nullptr, nullptr);
  out.pop_back();
  return out;
}

int CALLBACK enumFontProc(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM param) {
  auto* names = reinterpret_cast<std::set<std::wstring>*>(param);
  if (lf->lfFaceName[0] != L'@') names->insert(lf->lfFaceName);
  return 1;
}

const char* vertexShader = R"GLSL(#version 410 core
layout(location=0) in vec4 vPosition;
layout(location=1) in vec2 vUV;
out vec2 uv;
void main(){ gl_Position=vPosition; uv=vUV; }
)GLSL";

const char* fragmentShader = R"GLSL(#version 410 core
uniform sampler2D textTex;
uniform vec2 resolution;
uniform vec2 textSize;
uniform vec2 boxSize;
uniform vec2 boxCenter;
uniform vec2 paddingPx;
uniform vec4 boxColor;
uniform vec4 boxStrokeColor;
uniform float radiusPx;
uniform float boxStrokeWidth;
uniform vec2 boxStrokeOffset;
uniform float boxStrokeEnabled;
uniform float strokePosition;
uniform float rotationRad;
uniform float globalScale;
uniform float elapsed;
uniform float motionMode;
uniform float repeatMode;
uniform float motionSpeed;
uniform float loopGap;
uniform float flickerMode;
uniform float flickerSpeed;
uniform float flickerAmount;
uniform float flickerMinimum;
uniform float wiggleEnabled;
uniform vec2 wiggleAmount;
uniform float wiggleRotation;
uniform float wiggleSpeed;
uniform vec2 entranceOffset;

in vec2 uv;
out vec4 fragColor;

float roundedBox(vec2 p, vec2 halfSize, float radius){
  vec2 q=abs(p)-halfSize+vec2(radius);
  return length(max(q,0.0))+min(max(q.x,q.y),0.0)-radius;
}
float hash(float n){ return fract(sin(n)*43758.5453123); }

void main(){
  vec2 pixel=uv*resolution;
  vec2 p=(pixel-boxCenter-entranceOffset)/max(globalScale,0.001);
  float cs=cos(-rotationRad), sn=sin(-rotationRad);
  p=mat2(cs,-sn,sn,cs)*p;

  vec2 halfBox=boxSize*0.5;
  float d=roundedBox(p,halfBox,radiusPx);
  vec4 result=vec4(0.0);
  if(d<=0.0) result=boxColor;

  if(boxStrokeEnabled>0.5){
    vec2 sp=p-boxStrokeOffset;
    float sd=roundedBox(sp,halfBox,radiusPx);
    float shift=(strokePosition-1.0)*boxStrokeWidth*0.5;
    float band=1.0-smoothstep(boxStrokeWidth-0.75,boxStrokeWidth+0.75,abs(sd+shift));
    result=mix(result,boxStrokeColor,band*boxStrokeColor.a);
  }

  if(d<=0.0){
    vec2 inner=max(boxSize-paddingPx*2.0,vec2(1.0));
    vec2 pos=p+halfBox-paddingPx;
    float cycleX=textSize.x+max(loopGap,0.0);
    float cycleY=textSize.y+max(loopGap,0.0);
    float travel=elapsed*motionSpeed;
    if(repeatMode>0.5 && motionMode==1.0) pos.x+=mod(travel+inner.x,cycleX)-inner.x;
    else if(repeatMode>0.5 && motionMode==2.0) pos.x-=mod(travel+textSize.x,cycleX)-textSize.x;
    else if(repeatMode>0.5 && motionMode==3.0) pos.y+=mod(travel+inner.y,cycleY)-inner.y;
    else if(repeatMode>0.5 && motionMode==4.0) pos.y-=mod(travel+textSize.y,cycleY)-textSize.y;
    else if(motionMode==5.0){ float r=max(inner.x-textSize.x,0.0); pos.x-=r*0.5; pos.x+=r*(0.5+0.5*sin(travel/max(r,1.0)*3.14159265)); }
    else if(motionMode==6.0){ float r=max(inner.y-textSize.y,0.0); pos.y-=r*0.5; pos.y+=r*(0.5+0.5*sin(travel/max(r,1.0)*3.14159265)); }
    else if(motionMode==0.0) { pos-=max(inner-textSize,vec2(0.0))*0.5; }

    vec2 tw=pos;
    if(repeatMode<0.5){
      if(motionMode==1.0) tw.x=mod(pos.x+travel,cycleX);
      else if(motionMode==2.0) tw.x=mod(pos.x-travel+cycleX*1024.0,cycleX);
      else if(motionMode==3.0) tw.y=mod(pos.y+travel,cycleY);
      else if(motionMode==4.0) tw.y=mod(pos.y-travel+cycleY*1024.0,cycleY);
    }
    if(wiggleEnabled>0.5){
      float wave=elapsed*wiggleSpeed+tw.x*0.025;
      tw+=vec2(sin(wave),cos(wave*1.17))*wiggleAmount;
    }
    vec2 tuv=tw/max(textSize,vec2(1.0));
    if(all(greaterThanEqual(tuv,vec2(0.0))) && all(lessThanEqual(tuv,vec2(1.0)))){
      tuv.y=1.0-tuv.y;
      vec4 txt=texture(textTex,tuv);
      float cell=floor(tw.x/max(textSize.y*0.45,8.0));
      float f=1.0;
      if(flickerMode==1.0) f=mix(1.0,smoothstep(0.0,1.0,0.5+0.5*sin(elapsed*flickerSpeed-cell*0.7)),flickerAmount);
      else if(flickerMode==2.0){ float tick=floor(elapsed*flickerSpeed); f=mix(1.0,mix(flickerMinimum,1.0,hash(cell*17.0+tick)),flickerAmount); }
      else if(flickerMode==3.0) f=mix(1.0,mix(flickerMinimum,1.0,0.5+0.5*sin(elapsed*flickerSpeed)),flickerAmount);
      txt*=max(f,flickerMinimum);
      result=txt+result*(1.0-txt.a);
    }
  }
  fragColor=result;
}
)GLSL";
}

BRRunningText::BRRunningText() : values_(P_COUNT, 0.0f) {
  SetMinInputs(0); SetMaxInputs(0);
  GdiplusStartupInput startup; GdiplusStartup(&gdiplusToken_, &startup, nullptr);
  enumerateFonts();

  values_[P_FONT_SIZE]=0.105f; values_[P_ALIGN]=1.0f; values_[P_LINE_SPACING]=0.5f;
  values_[P_TEXT_B]=1.0f; values_[P_TEXT_A]=1.0f;
  values_[P_TEXT_STROKE_WIDTH]=0.08f; values_[P_TEXT_STROKE_SCALE]=0.27f; values_[P_TEXT_STROKE_B]=0.0f; values_[P_TEXT_STROKE_A]=1.0f;
  values_[P_BOX_MODE]=1.0f; values_[P_BOX_WIDTH]=1.0f; values_[P_BOX_HEIGHT]=0.18f;
  values_[P_PAD_X]=0.03f; values_[P_PAD_Y]=0.025f; values_[P_BOX_B]=0.12f; values_[P_BOX_A]=1.0f; values_[P_BOX_OPACITY]=0.85f; values_[P_RADIUS]=0.04f;
  values_[P_BOX_STROKE_WIDTH]=0.02f; values_[P_BOX_STROKE_B]=1.0f; values_[P_BOX_STROKE_A]=1.0f;
  values_[P_BOX_STROKE_POSITION]=1.0f;
  values_[P_BOX_STROKE_OFFSET_X]=0.5f; values_[P_BOX_STROKE_OFFSET_Y]=0.5f;
  values_[P_POS_X]=0.5f; values_[P_POS_Y]=0.5f; values_[P_SCALE]=0.5f;
  values_[P_MOTION]=1.0f; values_[P_SEPARATOR]=1.0f; values_[P_SPEED]=0.18f; values_[P_FLICKER_SPEED]=0.2f; values_[P_FLICKER_AMOUNT]=0.5f;
  values_[P_FLICKER_MIN]=0.25f; values_[P_WIGGLE_SPEED]=0.15f;
  values_[P_IN_DISTANCE]=1.0f; values_[P_IN_DURATION]=0.25f; values_[P_OUT_DISTANCE]=1.0f;
  values_[P_OUT_DURATION]=0.25f; values_[P_HOLD_DURATION]=0.5f;

  SetParamInfof(P_START,"Start",FF_TYPE_EVENT); SetParamInfof(P_PAUSE,"Pause",FF_TYPE_EVENT); SetParamInfof(P_RESUME,"Resume",FF_TYPE_EVENT); SetParamInfof(P_END,"End",FF_TYPE_EVENT);
  SetOptionParamInfo(P_PLAYBACK_STYLE,"Playback",2,0); addOption(P_PLAYBACK_STYLE,0,"Loop",0); addOption(P_PLAYBACK_STYLE,1,"Bounce",1);
  SetOptionParamInfo(P_MOTION,"Direction",5,1); const char* motions[]={"Static","Left","Right","Up Credit","Down Credit"}; for(unsigned i=0;i<5;++i)addOption(P_MOTION,i,motions[i],float(i));
  SetOptionParamInfo(P_REPEAT_MODE,"Repeat Mode",2,0); addOption(P_REPEAT_MODE,0,"Continuous Repeat",0); addOption(P_REPEAT_MODE,1,"Single Repeat",1);
  SetParamInfof(P_SEPARATOR,"Show Separator |",FF_TYPE_BOOLEAN);
  SetParamInfof(P_SPEED,"Speed",FF_TYPE_STANDARD); SetParamInfof(P_LOOP_GAP,"Continuous Loop Gap",FF_TYPE_STANDARD); SetParamInfof(P_EDGE_DELAY,"Single Edge Delay",FF_TYPE_STANDARD); SetParamInfof(P_RESET,"Restart",FF_TYPE_EVENT);

  SetParamInfof(P_EDIT_TEXT,"Edit Text",FF_TYPE_EVENT);
  SetParamInfo(P_TEXT,"Text Preview / Preset",FF_TYPE_TEXT,text_.c_str());
  SetParamInfo(P_FONT_NAME,"Font Search or Name",FF_TYPE_TEXT,fontName_.c_str());
  SetOptionParamInfo(P_FONT_LIST,"Installed Font",static_cast<unsigned int>(std::max<size_t>(fonts_.size(),1)),0.0f);
  if(fonts_.empty()) addOption(P_FONT_LIST,0,"Arial",0); else for(unsigned int i=0;i<fonts_.size();++i) addOption(P_FONT_LIST,i,fonts_[i].c_str(),float(i));
  SetParamInfof(P_REFRESH_FONTS,"Refresh Fonts",FF_TYPE_EVENT);
  SetParamInfof(P_FONT_SIZE,"Font Size",FF_TYPE_STANDARD);
  SetOptionParamInfo(P_ALIGN,"Align",3,1); addOption(P_ALIGN,0,"Left",0); addOption(P_ALIGN,1,"Center",1); addOption(P_ALIGN,2,"Right",2);
  SetParamInfof(P_LETTER_SPACING,"Letter Spacing",FF_TYPE_STANDARD); SetParamInfof(P_LINE_SPACING,"Line Spacing",FF_TYPE_STANDARD);
  SetParamInfof(P_TEXT_H,"Text Color",FF_TYPE_HUE); SetParamInfof(P_TEXT_S,"Text Color Sat",FF_TYPE_SATURATION); SetParamInfof(P_TEXT_B,"Text Color Bri",FF_TYPE_BRIGHTNESS); SetParamInfof(P_TEXT_A,"Text Color Alpha",FF_TYPE_ALPHA);
  SetParamInfof(P_TEXT_STROKE,"Text Stroke",FF_TYPE_BOOLEAN); SetParamInfof(P_TEXT_STROKE_WIDTH,"Text Stroke Width",FF_TYPE_STANDARD); SetParamInfof(P_TEXT_STROKE_SCALE,"Stroke Scale",FF_TYPE_STANDARD);
  SetParamInfof(P_TEXT_STROKE_H,"Stroke Color",FF_TYPE_HUE); SetParamInfof(P_TEXT_STROKE_S,"Stroke Color Sat",FF_TYPE_SATURATION); SetParamInfof(P_TEXT_STROKE_B,"Stroke Color Bri",FF_TYPE_BRIGHTNESS); SetParamInfof(P_TEXT_STROKE_A,"Stroke Color Alpha",FF_TYPE_ALPHA);
  SetOptionParamInfo(P_BOX_MODE,"Box Size Mode",3,1); addOption(P_BOX_MODE,0,"Auto Size",0); addOption(P_BOX_MODE,1,"Auto Height",1); addOption(P_BOX_MODE,2,"Custom Size",2);
  SetParamInfof(P_BOX_WIDTH,"Box Width",FF_TYPE_STANDARD); SetParamInfof(P_BOX_HEIGHT,"Box Height",FF_TYPE_STANDARD); SetParamInfof(P_PAD_X,"Padding Left Right",FF_TYPE_STANDARD); SetParamInfof(P_PAD_Y,"Padding Top Bottom",FF_TYPE_STANDARD);
  SetParamInfof(P_BOX_H,"Box Color",FF_TYPE_HUE); SetParamInfof(P_BOX_S,"Box Color Sat",FF_TYPE_SATURATION); SetParamInfof(P_BOX_B,"Box Color Bri",FF_TYPE_BRIGHTNESS); SetParamInfof(P_BOX_A,"Box Color Alpha",FF_TYPE_ALPHA); SetParamInfof(P_BOX_OPACITY,"Box Opacity",FF_TYPE_STANDARD); SetParamInfof(P_RADIUS,"Corner Radius",FF_TYPE_STANDARD);
  SetParamInfof(P_BOX_STROKE,"Box Stroke",FF_TYPE_BOOLEAN); SetParamInfof(P_BOX_STROKE_WIDTH,"Box Stroke Width",FF_TYPE_STANDARD);
  SetParamInfof(P_BOX_STROKE_H,"Box Stroke Color",FF_TYPE_HUE); SetParamInfof(P_BOX_STROKE_S,"Box Stroke Sat",FF_TYPE_SATURATION); SetParamInfof(P_BOX_STROKE_B,"Box Stroke Bri",FF_TYPE_BRIGHTNESS); SetParamInfof(P_BOX_STROKE_A,"Box Stroke Alpha",FF_TYPE_ALPHA);
  SetOptionParamInfo(P_BOX_STROKE_POSITION,"Stroke Position",3,1); addOption(P_BOX_STROKE_POSITION,0,"Inside",0); addOption(P_BOX_STROKE_POSITION,1,"Center",1); addOption(P_BOX_STROKE_POSITION,2,"Outside",2);
  SetParamInfof(P_BOX_STROKE_OFFSET_X,"Stroke Offset X",FF_TYPE_STANDARD); SetParamInfof(P_BOX_STROKE_OFFSET_Y,"Stroke Offset Y",FF_TYPE_STANDARD);
  SetParamInfof(P_POS_X,"Position X",FF_TYPE_XPOS); SetParamInfof(P_POS_Y,"Position Y",FF_TYPE_YPOS); SetParamInfof(P_SCALE,"Scale",FF_TYPE_STANDARD);
  SetOptionParamInfo(P_ROTATION,"Rotation",4,0); addOption(P_ROTATION,0,"0 degrees",0); addOption(P_ROTATION,1,"90 degrees",1); addOption(P_ROTATION,2,"180 degrees",2); addOption(P_ROTATION,3,"270 degrees",3);
  SetOptionParamInfo(P_FLICKER,"Flicker",4,0); addOption(P_FLICKER,0,"Off",0); addOption(P_FLICKER,1,"Sequential",1); addOption(P_FLICKER,2,"Random",2); addOption(P_FLICKER,3,"All",3);
  SetParamInfof(P_FLICKER_SPEED,"Flicker Speed",FF_TYPE_STANDARD); SetParamInfof(P_FLICKER_AMOUNT,"Flicker Amount",FF_TYPE_STANDARD); SetParamInfof(P_FLICKER_MIN,"Flicker Minimum",FF_TYPE_STANDARD);
  SetParamInfof(P_WIGGLE,"Wiggle",FF_TYPE_BOOLEAN); SetParamInfof(P_WIGGLE_X,"Wiggle X",FF_TYPE_STANDARD); SetParamInfof(P_WIGGLE_Y,"Wiggle Y",FF_TYPE_STANDARD); SetParamInfof(P_WIGGLE_ROTATION,"Wiggle Rotation",FF_TYPE_STANDARD); SetParamInfof(P_WIGGLE_SPEED,"Wiggle Speed",FF_TYPE_STANDARD);
  SetParamInfof(P_ANIM_IN,"Animation In",FF_TYPE_BOOLEAN); SetOptionParamInfo(P_IN_AXIS,"In Axis",2,0); addOption(P_IN_AXIS,0,"Scroll X",0); addOption(P_IN_AXIS,1,"Scroll Y",1); SetParamInfof(P_IN_INVERT,"Invert In",FF_TYPE_BOOLEAN); SetParamInfof(P_IN_DISTANCE,"In Distance",FF_TYPE_STANDARD); SetParamInfof(P_IN_DURATION,"In Duration",FF_TYPE_STANDARD); SetParamInfof(P_IN_DELAY,"In Delay",FF_TYPE_STANDARD);
  SetOptionParamInfo(P_IN_EASE,"In Easing",4,3); const char* ease[]={"Linear","Ease In","Ease Out","Ease In Out"}; for(unsigned i=0;i<4;++i)addOption(P_IN_EASE,i,ease[i],float(i));
  SetParamInfof(P_ANIM_OUT,"Animation Out",FF_TYPE_BOOLEAN); SetOptionParamInfo(P_OUT_AXIS,"Out Axis",2,0); addOption(P_OUT_AXIS,0,"Scroll X",0); addOption(P_OUT_AXIS,1,"Scroll Y",1); SetParamInfof(P_OUT_INVERT,"Invert Out",FF_TYPE_BOOLEAN); SetParamInfof(P_OUT_DISTANCE,"Out Distance",FF_TYPE_STANDARD); SetParamInfof(P_OUT_DURATION,"Out Duration",FF_TYPE_STANDARD); SetParamInfof(P_OUT_DELAY,"Out Delay",FF_TYPE_STANDARD);
  SetOptionParamInfo(P_OUT_EASE,"Out Easing",4,3); for(unsigned i=0;i<4;++i)addOption(P_OUT_EASE,i,ease[i],float(i)); SetOptionParamInfo(P_OUT_TRIGGER,"Out Trigger",2,0); addOption(P_OUT_TRIGGER,0,"Manual",0); addOption(P_OUT_TRIGGER,1,"Auto Timer",1); SetParamInfof(P_HOLD_DURATION,"Hold Duration",FF_TYPE_STANDARD); SetParamInfof(P_PLAY_OUT,"Play Out",FF_TYPE_EVENT);
  SetParamInfo(P_CREDIT,"Effect by Belajar Resolume",FF_TYPE_TEXT,credit_.c_str());
  for(unsigned int i=P_START;i<=P_RESET;++i) SetParamGroup(i,"Animation");
  for(unsigned int i=P_EDIT_TEXT;i<=P_TEXT_STROKE_A;++i) SetParamGroup(i,"Text Setting");
  for(unsigned int i=P_BOX_MODE;i<=P_ROTATION;++i) SetParamGroup(i,"Box Setting");
  for(unsigned int i=P_FLICKER;i<=P_WIGGLE_SPEED;++i) SetParamGroup(i,"Effect");
  for(unsigned int i=P_ANIM_IN;i<=P_PLAY_OUT;++i) SetParamGroup(i,"Animation In Out");
  SetParamGroup(P_CREDIT,"Credit");
  started_=lastFrame_=std::chrono::steady_clock::now();
}

BRRunningText::~BRRunningText(){
  editorStopRequested_.store(true);
  if(HWND window=editorWindow_.load())PostMessageW(window,WM_CLOSE,0,0);
  if(editorThread_.joinable())editorThread_.join();
  if(gdiplusToken_)GdiplusShutdown(gdiplusToken_);
}
void BRRunningText::addOption(unsigned int p,unsigned int e,const char* n,float v){ SetParamElementInfo(p,e,n,v); }

void BRRunningText::openTextEditor(){
  if(editorRunning_.load()){
    if(HWND window=editorWindow_.load())PostMessageW(window,WM_APP+1,0,0);
    return;
  }
  if(editorThread_.joinable())editorThread_.join();
  editorStopRequested_.store(false); editorRunning_.store(true);
  editorThread_=std::thread(&BRRunningText::runTextEditor,this);
}

void BRRunningText::runTextEditor(){
  static const wchar_t* className=L"BRRunningTextEditorWindow";
  HINSTANCE instance=GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=EditorWndProc; wc.hInstance=instance;
  wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1); wc.lpszClassName=className;
  if(RegisterClassExW(&wc)==0 && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){editorRunning_.store(false);return;}
  HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,className,L"BR Running Text — Text Editor",
    WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,760,520,nullptr,nullptr,instance,this);
  if(!window){editorRunning_.store(false);return;}
  editorWindow_.store(window);
  if(editorStopRequested_.load())PostMessageW(window,WM_CLOSE,0,0);
  MSG message{};
  while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
  editorWindow_.store(nullptr); editorRunning_.store(false);
}

LRESULT CALLBACK BRRunningText::EditorWndProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
  auto* self=reinterpret_cast<BRRunningText*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  if(message==WM_NCCREATE){
    auto* create=reinterpret_cast<CREATESTRUCTW*>(lParam); self=reinterpret_cast<BRRunningText*>(create->lpCreateParams);
    SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
  }
  if(!self)return DefWindowProcW(window,message,wParam,lParam);
  constexpr int ID_TEXT=1001,ID_UPDATE=1002,ID_CANCEL=1003;
  switch(message){
    case WM_CREATE:{
      CreateWindowExW(0,L"STATIC",L"Tulis teks di bawah. Tekan Enter untuk membuat baris baru.",WS_CHILD|WS_VISIBLE,
        16,14,700,22,window,nullptr,nullptr,nullptr);
      self->editorTextBox_=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|
        ES_LEFT|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL,16,42,710,380,window,reinterpret_cast<HMENU>(ID_TEXT),nullptr,nullptr);
      HWND cancel=CreateWindowExW(0,L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
        526,434,96,34,window,reinterpret_cast<HMENU>(ID_CANCEL),nullptr,nullptr);
      HWND update=CreateWindowExW(0,L"BUTTON",L"Update Text",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
        630,434,96,34,window,reinterpret_cast<HMENU>(ID_UPDATE),nullptr,nullptr);
      HFONT font=reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
      SendMessageW(self->editorTextBox_,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
      SendMessageW(cancel,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); SendMessageW(update,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
      std::string currentText; {std::lock_guard<std::mutex> lock(self->textMutex_);currentText=self->text_;}
      std::wstring current=utf8ToWide(currentText); SetWindowTextW(self->editorTextBox_,current.c_str());
      SetFocus(self->editorTextBox_); return 0;
    }
    case WM_SIZE:{
      int width=LOWORD(lParam),height=HIWORD(lParam); int buttonY=std::max(80,height-50);
      if(self->editorTextBox_)MoveWindow(self->editorTextBox_,16,42,std::max(120,width-32),std::max(40,height-104),TRUE);
      HWND cancel=GetDlgItem(window,ID_CANCEL),update=GetDlgItem(window,ID_UPDATE);
      if(cancel)MoveWindow(cancel,std::max(16,width-216),buttonY,96,34,TRUE);
      if(update)MoveWindow(update,std::max(120,width-112),buttonY,96,34,TRUE); return 0;
    }
    case WM_COMMAND:
      if(LOWORD(wParam)==ID_UPDATE){
        int length=GetWindowTextLengthW(self->editorTextBox_); std::wstring updated(static_cast<size_t>(length)+1,L'\0');
        GetWindowTextW(self->editorTextBox_,updated.data(),length+1); updated.resize(static_cast<size_t>(length));
        {std::lock_guard<std::mutex> lock(self->textMutex_);self->text_=wideToUtf8(updated);}
        self->textureDirty_.store(true);
        DestroyWindow(window); return 0;
      }
      if(LOWORD(wParam)==ID_CANCEL){DestroyWindow(window);return 0;} break;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_APP+1: ShowWindow(window,SW_RESTORE); SetForegroundWindow(window); SetFocus(self->editorTextBox_); return 0;
    case WM_NCDESTROY: self->editorTextBox_=nullptr; self->editorWindow_.store(nullptr); SetWindowLongPtrW(window,GWLP_USERDATA,0); PostQuitMessage(0); break;
  }
  return DefWindowProcW(window,message,wParam,lParam);
}

void BRRunningText::enumerateFonts(){
  std::set<std::wstring> names; HDC dc=GetDC(nullptr); LOGFONTW lf{}; lf.lfCharSet=DEFAULT_CHARSET;
  EnumFontFamiliesExW(dc,&lf,reinterpret_cast<FONTENUMPROCW>(enumFontProc),reinterpret_cast<LPARAM>(&names),0); ReleaseDC(nullptr,dc);
  fonts_.clear(); for(const auto& n:names) fonts_.push_back(wideToUtf8(n));
}

FFResult BRRunningText::InitGL(const FFGLViewportStruct* vp){
  viewportWidth_=vp->width; viewportHeight_=vp->height;
  if(!shader_.Compile(vertexShader,fragmentShader)||!quad_.Initialise()) return FF_FAIL;
  textureDirty_.store(false); rebuildTextTexture(); return CFFGLPlugin::InitGL(vp);
}

void BRRunningText::releaseTextTexture(){ if(textTexture_){ glDeleteTextures(1,&textTexture_); textTexture_=0; } }

void BRRunningText::rebuildTextTexture(){
  releaseTextTexture();
  const float fontPx=18.0f+values_[P_FONT_SIZE]*220.0f;
  std::string textCopy,fontCopy; {std::lock_guard<std::mutex> lock(textMutex_);textCopy=text_;fontCopy=fontName_;}
  std::string displayText=expandLineBreaks(textCopy);
  if(values_[P_REPEAT_MODE]<0.5f && values_[P_SEPARATOR]>0.5f && displayText.find('\n')==std::string::npos) displayText += " |";
  std::wstring content=utf8ToWide(displayText); if(content.empty()) content=L" ";
  std::wstring familyName=utf8ToWide(fontCopy); FontFamily selectedFamily(familyName.c_str()); FontFamily fallbackFamily(L"Arial");
  FontFamily* family=selectedFamily.IsAvailable()?&selectedFamily:&fallbackFamily;
  StringFormat format(StringFormat::GenericTypographic()); format.SetFormatFlags(StringFormatFlagsMeasureTrailingSpaces);
  Bitmap measure(16,16,PixelFormat32bppARGB); Graphics mg(&measure); GraphicsPath path;
  path.AddString(content.c_str(),-1,family,FontStyleRegular,fontPx,PointF(0,0),&format);
  RectF bounds; path.GetBounds(&bounds);
  float strokeScale=0.25f+values_[P_TEXT_STROKE_SCALE]*2.75f;
  float stroke=values_[P_TEXT_STROKE]>0.5f ? (1.0f+values_[P_TEXT_STROKE_WIDTH]*18.0f)*strokeScale : 0.0f;
  textWidth_=std::max(2u,static_cast<unsigned int>(std::ceil(bounds.Width+stroke*4+8)));
  textHeight_=std::max(2u,static_cast<unsigned int>(std::ceil(bounds.Height+stroke*4+8)));
  textWidth_=std::min(textWidth_,8192u); textHeight_=std::min(textHeight_,8192u);
  Bitmap bmp(textWidth_,textHeight_,PixelFormat32bppARGB); Graphics g(&bmp); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit); g.Clear(Color(0,0,0,0));
  GraphicsPath draw; draw.AddString(content.c_str(),-1,family,FontStyleRegular,fontPx,PointF(4-bounds.X+stroke*2,4-bounds.Y+stroke*2),&format);
  auto hsbaToColor=[&](int h,int s,int b,int a){ float rgb[3]; HSVtoRGB(values_[h],values_[s],values_[b],rgb[0],rgb[1],rgb[2]); return Color(BYTE(brClamp01(values_[a])*255),BYTE(brClamp01(rgb[0])*255),BYTE(brClamp01(rgb[1])*255),BYTE(brClamp01(rgb[2])*255)); };
  if(stroke>0){ Pen pen(hsbaToColor(P_TEXT_STROKE_H,P_TEXT_STROKE_S,P_TEXT_STROKE_B,P_TEXT_STROKE_A),stroke); pen.SetLineJoin(LineJoinRound); g.DrawPath(&pen,&draw); }
  SolidBrush fill(hsbaToColor(P_TEXT_H,P_TEXT_S,P_TEXT_B,P_TEXT_A)); g.FillPath(&fill,&draw);
  Rect lockRect(0,0,textWidth_,textHeight_); BitmapData data{}; bmp.LockBits(&lockRect,ImageLockModeRead,PixelFormat32bppARGB,&data);
  GLint previousTexture=0, previousAlignment=4, previousRowLength=0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D,&previousTexture); glGetIntegerv(GL_UNPACK_ALIGNMENT,&previousAlignment); glGetIntegerv(GL_UNPACK_ROW_LENGTH,&previousRowLength);
  glGenTextures(1,&textTexture_); glBindTexture(GL_TEXTURE_2D,textTexture_); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT,4); glPixelStorei(GL_UNPACK_ROW_LENGTH,0); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,textWidth_,textHeight_,0,GL_BGRA,GL_UNSIGNED_BYTE,data.Scan0); glPixelStorei(GL_UNPACK_ALIGNMENT,previousAlignment); glPixelStorei(GL_UNPACK_ROW_LENGTH,previousRowLength); glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(previousTexture)); bmp.UnlockBits(&data);
}

FFResult BRRunningText::ProcessOpenGL(ProcessOpenGLStruct*){
  if(!visible_) return FF_SUCCESS;
  if(textureDirty_.exchange(false)) rebuildTextTexture();
  auto now=std::chrono::steady_clock::now(); double dt=std::chrono::duration<double>(now-lastFrame_).count(); lastFrame_=now; if(!paused_) elapsed_+=std::min(dt,0.1);
  float boxW=values_[P_BOX_WIDTH]*viewportWidth_, boxH=values_[P_BOX_HEIGHT]*viewportHeight_;
  float padX=values_[P_PAD_X]*viewportWidth_*0.25f, padY=values_[P_PAD_Y]*viewportHeight_*0.25f;
  int mode=int(values_[P_BOX_MODE]+0.5f); if(mode==0){boxW=textWidth_+2*padX; boxH=textHeight_+2*padY;} else if(mode==1) boxH=textHeight_+2*padY;
  boxW=std::max(boxW,8.0f); boxH=std::max(boxH,8.0f);
  auto ease=[](float t,int m){t=brClamp01(t); if(m==1)return t*t; if(m==2)return 1-(1-t)*(1-t); if(m==3)return t*t*(3-2*t); return t;};
  float inOffsetX=0,inOffsetY=0; double t=elapsed_;
  if(values_[P_ANIM_IN]>0.5f){float delay=values_[P_IN_DELAY]*5, dur=0.05f+values_[P_IN_DURATION]*5; float k=ease(float((t-delay)/dur),int(values_[P_IN_EASE]+.5f)); float dist=values_[P_IN_DISTANCE]*(values_[P_IN_AXIS]<.5f?viewportWidth_:viewportHeight_); float off=(1-k)*dist*(values_[P_IN_INVERT]>.5f?1:-1); if(values_[P_IN_AXIS]<.5f)inOffsetX=off;else inOffsetY=off;}
  bool autoOut=values_[P_OUT_TRIGGER]>.5f; float inEnd=values_[P_IN_DELAY]*5+0.05f+values_[P_IN_DURATION]*5; float outStart=autoOut?inEnd+values_[P_HOLD_DURATION]*20+values_[P_OUT_DELAY]*5:0;
  if(values_[P_ANIM_OUT]>.5f && ((autoOut&&t>=outStart)||manualOut_)){double base=manualOut_?0:outStart; float dur=.05f+values_[P_OUT_DURATION]*5; float k=ease(float((t-base)/dur),int(values_[P_OUT_EASE]+.5f)); float dist=values_[P_OUT_DISTANCE]*(values_[P_OUT_AXIS]<.5f?viewportWidth_:viewportHeight_); float off=k*dist*(values_[P_OUT_INVERT]>.5f?-1:1); if(values_[P_OUT_AXIS]<.5f)inOffsetX=off;else inOffsetY=off; if(k>=1.0f)visible_=false;}
  float boxRgb[3], strokeRgb[3]; HSVtoRGB(values_[P_BOX_H],values_[P_BOX_S],values_[P_BOX_B],boxRgb[0],boxRgb[1],boxRgb[2]); HSVtoRGB(values_[P_BOX_STROKE_H],values_[P_BOX_STROKE_S],values_[P_BOX_STROKE_B],strokeRgb[0],strokeRgb[1],strokeRgb[2]);
  ScopedShaderBinding bind(shader_.GetGLID()); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,textTexture_);
  auto u=[&](const char* n){return shader_.FindUniform(n);};
  glUniform1i(u("textTex"),0); glUniform2f(u("resolution"),float(viewportWidth_),float(viewportHeight_)); glUniform2f(u("textSize"),float(textWidth_),float(textHeight_)); glUniform2f(u("boxSize"),boxW,boxH); glUniform2f(u("boxCenter"),values_[P_POS_X]*viewportWidth_,(1-values_[P_POS_Y])*viewportHeight_); glUniform2f(u("paddingPx"),padX,padY);
  glUniform4f(u("boxColor"),boxRgb[0],boxRgb[1],boxRgb[2],values_[P_BOX_A]*values_[P_BOX_OPACITY]); glUniform4f(u("boxStrokeColor"),strokeRgb[0],strokeRgb[1],strokeRgb[2],values_[P_BOX_STROKE_A]); glUniform1f(u("radiusPx"),values_[P_RADIUS]*std::min(boxW,boxH)*.5f); glUniform1f(u("boxStrokeWidth"),values_[P_BOX_STROKE_WIDTH]*30); glUniform2f(u("boxStrokeOffset"),(values_[P_BOX_STROKE_OFFSET_X]-.5f)*100,(values_[P_BOX_STROKE_OFFSET_Y]-.5f)*100); glUniform1f(u("boxStrokeEnabled"),values_[P_BOX_STROKE]); glUniform1f(u("strokePosition"),values_[P_BOX_STROKE_POSITION]);
  float effectiveMotion=values_[P_MOTION]; if(values_[P_PLAYBACK_STYLE]>.5f){if(effectiveMotion==1.0f||effectiveMotion==2.0f)effectiveMotion=5.0f;else if(effectiveMotion==3.0f||effectiveMotion==4.0f)effectiveMotion=6.0f;}
  glUniform1f(u("rotationRad"),values_[P_ROTATION]*1.57079632679f); glUniform1f(u("globalScale"),.25f+values_[P_SCALE]*1.5f); glUniform1f(u("elapsed"),float(elapsed_)); glUniform1f(u("motionMode"),effectiveMotion); glUniform1f(u("repeatMode"),values_[P_REPEAT_MODE]); glUniform1f(u("motionSpeed"),20+values_[P_SPEED]*600); glUniform1f(u("loopGap"),values_[P_REPEAT_MODE]<.5f?values_[P_LOOP_GAP]*viewportWidth_*.5f:0.0f);
  glUniform1f(u("flickerMode"),values_[P_FLICKER]); glUniform1f(u("flickerSpeed"),.2f+values_[P_FLICKER_SPEED]*18); glUniform1f(u("flickerAmount"),values_[P_FLICKER_AMOUNT]); glUniform1f(u("flickerMinimum"),values_[P_FLICKER_MIN]); glUniform1f(u("wiggleEnabled"),values_[P_WIGGLE]); glUniform2f(u("wiggleAmount"),values_[P_WIGGLE_X]*20,values_[P_WIGGLE_Y]*20); glUniform1f(u("wiggleRotation"),values_[P_WIGGLE_ROTATION]); glUniform1f(u("wiggleSpeed"),.2f+values_[P_WIGGLE_SPEED]*15); glUniform2f(u("entranceOffset"),inOffsetX,inOffsetY);
  glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA); quad_.Draw(); glDisable(GL_BLEND); glBindTexture(GL_TEXTURE_2D,0); return FF_SUCCESS;
}

FFResult BRRunningText::DeInitGL(){ releaseTextTexture(); shader_.FreeGLResources(); quad_.Release(); return FF_SUCCESS; }

FFResult BRRunningText::SetFloatParameter(unsigned int i,float v){
  if(i>=P_COUNT)return FF_FAIL; values_[i]=v;
  if(i==P_EDIT_TEXT&&v!=0)openTextEditor();
  if(i==P_FONT_LIST&&!fonts_.empty()){unsigned n=std::min<unsigned>(unsigned(v+.5f),unsigned(fonts_.size()-1));{std::lock_guard<std::mutex> lock(textMutex_);fontName_=fonts_[n];}textureDirty_.store(true);RaiseParamEvent(P_FONT_NAME,FF_EVENT_FLAG_VALUE);}
  if(i==P_REFRESH_FONTS&&v!=0){enumerateFonts();}
  if(i==P_START&&v!=0){visible_=true;paused_=false;manualOut_=false;elapsed_=0;lastFrame_=std::chrono::steady_clock::now();}
  if(i==P_PAUSE&&v!=0){paused_=true;lastFrame_=std::chrono::steady_clock::now();}
  if(i==P_RESUME&&v!=0){visible_=true;paused_=false;lastFrame_=std::chrono::steady_clock::now();}
  if(i==P_END&&v!=0){if(values_[P_ANIM_OUT]>.5f){manualOut_=true;paused_=false;elapsed_=0;lastFrame_=std::chrono::steady_clock::now();}else visible_=false;}
  if(i==P_RESET&&v!=0){elapsed_=0;manualOut_=false;}
  if(i==P_PLAY_OUT&&v!=0){manualOut_=true;paused_=false;elapsed_=0;lastFrame_=std::chrono::steady_clock::now();}
  if((i>=P_FONT_SIZE&&i<=P_TEXT_STROKE_A)||i==P_FONT_LIST||i==P_SEPARATOR||i==P_REPEAT_MODE)textureDirty_.store(true);
  return FF_SUCCESS;
}
float BRRunningText::GetFloatParameter(unsigned int i){return i<P_COUNT?values_[i]:0;}
FFResult BRRunningText::SetTextParameter(unsigned int i,const char* v){if(i==P_TEXT){std::lock_guard<std::mutex> lock(textMutex_);text_=v?v:"";textureDirty_.store(true);return FF_SUCCESS;}if(i==P_FONT_NAME){std::lock_guard<std::mutex> lock(textMutex_);fontName_=v?v:"Arial";textureDirty_.store(true);return FF_SUCCESS;}if(i==P_CREDIT)return FF_SUCCESS;return FF_FAIL;}
char* BRRunningText::GetTextParameter(unsigned int i){static thread_local char buffer[8192];std::string value;if(i==P_CREDIT)value=credit_;else{std::lock_guard<std::mutex> lock(textMutex_);value=i==P_TEXT?text_:fontName_;}const size_t length=std::min(value.size(),sizeof(buffer)-1);std::memcpy(buffer,value.data(),length);buffer[length]=0;return buffer;}
