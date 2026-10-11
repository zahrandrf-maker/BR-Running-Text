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
  P_TEXT, P_FONT_NAME, P_FONT_LIST, P_REFRESH_FONTS,
  P_FONT_SIZE, P_ALIGN, P_LETTER_SPACING, P_LINE_SPACING,
  P_TEXT_H, P_TEXT_S, P_TEXT_B, P_TEXT_A,
  P_TEXT_STROKE, P_TEXT_STROKE_WIDTH, P_TEXT_STROKE_H, P_TEXT_STROKE_S,
  P_TEXT_STROKE_B, P_TEXT_STROKE_A,
  P_BOX_MODE, P_BOX_WIDTH, P_BOX_HEIGHT, P_PAD_X, P_PAD_Y,
  P_BOX_H, P_BOX_S, P_BOX_B, P_BOX_A, P_RADIUS,
  P_BOX_STROKE, P_BOX_STROKE_WIDTH, P_BOX_STROKE_H, P_BOX_STROKE_S,
  P_BOX_STROKE_B, P_BOX_STROKE_A, P_BOX_STROKE_POSITION,
  P_BOX_STROKE_OFFSET_X, P_BOX_STROKE_OFFSET_Y,
  P_POS_X, P_POS_Y, P_SCALE, P_ROTATION,
  P_MOTION, P_SPEED, P_PAUSE, P_LOOP_GAP, P_EDGE_DELAY, P_RESET,
  P_FLICKER, P_FLICKER_SPEED, P_FLICKER_AMOUNT, P_FLICKER_MIN,
  P_WIGGLE, P_WIGGLE_X, P_WIGGLE_Y, P_WIGGLE_ROTATION, P_WIGGLE_SPEED,
  P_ANIM_IN, P_IN_AXIS, P_IN_INVERT, P_IN_DISTANCE, P_IN_DURATION, P_IN_DELAY, P_IN_EASE,
  P_ANIM_OUT, P_OUT_AXIS, P_OUT_INVERT, P_OUT_DISTANCE, P_OUT_DURATION,
  P_OUT_DELAY, P_OUT_EASE, P_OUT_TRIGGER, P_HOLD_DURATION, P_PLAY_OUT,
  P_CREDIT,
  P_COUNT
};

static CFFGLPluginInfo PluginInfo(
  PluginFactory<BRRunningText>, "BRRT", "BR Running Text",
  2, 1, 0, 100, FF_SOURCE,
  "Running text, credits and lower thirds by BELAJAR RESOLUME",
  "Effect by Belajar Resolume");

float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

std::wstring utf8ToWide(const std::string& value) {
  if (value.empty()) return L"";
  int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
  if (count <= 0) return std::wstring(value.begin(), value.end());
  std::wstring out(static_cast<size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), count);
  out.pop_back();
  return out;
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
    if(motionMode==1.0) pos.x+=mod(travel+inner.x,cycleX)-inner.x;
    else if(motionMode==2.0) pos.x-=mod(travel+textSize.x,cycleX)-textSize.x;
    else if(motionMode==3.0) pos.y+=mod(travel+inner.y,cycleY)-inner.y;
    else if(motionMode==4.0) pos.y-=mod(travel+textSize.y,cycleY)-textSize.y;
    else if(motionMode==5.0){ float r=max(inner.x-textSize.x,0.0); pos.x-=r*0.5; pos.x+=r*(0.5+0.5*sin(travel/max(r,1.0)*3.14159265)); }
    else if(motionMode==6.0){ float r=max(inner.y-textSize.y,0.0); pos.y-=r*0.5; pos.y+=r*(0.5+0.5*sin(travel/max(r,1.0)*3.14159265)); }
    else { pos-=max(inner-textSize,vec2(0.0))*0.5; }

    vec2 tw=pos;
    if(wiggleEnabled>0.5){
      float wave=elapsed*wiggleSpeed+tw.x*0.025;
      tw+=vec2(sin(wave),cos(wave*1.17))*wiggleAmount;
    }
    vec2 tuv=tw/max(textSize,vec2(1.0));
    if(all(greaterThanEqual(tuv,vec2(0.0))) && all(lessThanEqual(tuv,vec2(1.0)))){
      vec4 txt=texture(textTex,tuv);
      float cell=floor(tw.x/max(textSize.y*0.45,8.0));
      float f=1.0;
      if(flickerMode==1.0) f=mix(1.0,smoothstep(0.0,1.0,0.5+0.5*sin(elapsed*flickerSpeed-cell*0.7)),flickerAmount);
      else if(flickerMode==2.0){ float tick=floor(elapsed*flickerSpeed); f=mix(1.0,mix(flickerMinimum,1.0,hash(cell*17.0+tick)),flickerAmount); }
      else if(flickerMode==3.0) f=mix(1.0,mix(flickerMinimum,1.0,0.5+0.5*sin(elapsed*flickerSpeed)),flickerAmount);
      txt.a*=max(f,flickerMinimum);
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
  values_[P_TEXT_STROKE_WIDTH]=0.08f; values_[P_TEXT_STROKE_B]=0.0f; values_[P_TEXT_STROKE_A]=1.0f;
  values_[P_BOX_MODE]=1.0f; values_[P_BOX_WIDTH]=0.75f; values_[P_BOX_HEIGHT]=0.18f;
  values_[P_PAD_X]=0.03f; values_[P_PAD_Y]=0.025f; values_[P_BOX_A]=0.75f; values_[P_RADIUS]=0.04f;
  values_[P_BOX_STROKE_WIDTH]=0.02f; values_[P_BOX_STROKE_B]=1.0f; values_[P_BOX_STROKE_A]=1.0f;
  values_[P_BOX_STROKE_POSITION]=1.0f;
  values_[P_BOX_STROKE_OFFSET_X]=0.5f; values_[P_BOX_STROKE_OFFSET_Y]=0.5f;
  values_[P_POS_X]=0.5f; values_[P_POS_Y]=0.5f; values_[P_SCALE]=0.5f;
  values_[P_SPEED]=0.18f; values_[P_FLICKER_SPEED]=0.2f; values_[P_FLICKER_AMOUNT]=0.5f;
  values_[P_FLICKER_MIN]=0.25f; values_[P_WIGGLE_SPEED]=0.15f;
  values_[P_IN_DISTANCE]=1.0f; values_[P_IN_DURATION]=0.25f; values_[P_OUT_DISTANCE]=1.0f;
  values_[P_OUT_DURATION]=0.25f; values_[P_HOLD_DURATION]=0.5f;

  SetParamInfo(P_TEXT,"01 Text",FF_TYPE_TEXT,text_.c_str());
  SetParamInfo(P_FONT_NAME,"02 Font Search or Name",FF_TYPE_TEXT,fontName_.c_str());
  SetOptionParamInfo(P_FONT_LIST,"03 Installed Font",static_cast<unsigned int>(std::max<size_t>(fonts_.size(),1)),0.0f);
  if(fonts_.empty()) addOption(P_FONT_LIST,0,"Arial",0); else for(unsigned int i=0;i<fonts_.size();++i) addOption(P_FONT_LIST,i,fonts_[i].c_str(),float(i));
  SetParamInfof(P_REFRESH_FONTS,"04 Refresh Fonts",FF_TYPE_EVENT);
  SetParamInfof(P_FONT_SIZE,"05 Font Size",FF_TYPE_STANDARD);
  SetOptionParamInfo(P_ALIGN,"06 Align",3,1); addOption(P_ALIGN,0,"Left",0); addOption(P_ALIGN,1,"Center",1); addOption(P_ALIGN,2,"Right",2);
  SetParamInfof(P_LETTER_SPACING,"07 Letter Spacing",FF_TYPE_STANDARD); SetParamInfof(P_LINE_SPACING,"08 Line Spacing",FF_TYPE_STANDARD);
  SetParamInfof(P_TEXT_H,"09 Text Color",FF_TYPE_HUE); SetParamInfof(P_TEXT_S,"09 Text Color Sat",FF_TYPE_SATURATION); SetParamInfof(P_TEXT_B,"09 Text Color Bri",FF_TYPE_BRIGHTNESS); SetParamInfof(P_TEXT_A,"09 Text Color Alpha",FF_TYPE_ALPHA);
  SetParamInfof(P_TEXT_STROKE,"10 Text Stroke",FF_TYPE_BOOLEAN); SetParamInfof(P_TEXT_STROKE_WIDTH,"11 Text Stroke Width",FF_TYPE_STANDARD);
  SetParamInfof(P_TEXT_STROKE_H,"12 Stroke Color",FF_TYPE_HUE); SetParamInfof(P_TEXT_STROKE_S,"12 Stroke Color Sat",FF_TYPE_SATURATION); SetParamInfof(P_TEXT_STROKE_B,"12 Stroke Color Bri",FF_TYPE_BRIGHTNESS); SetParamInfof(P_TEXT_STROKE_A,"12 Stroke Color Alpha",FF_TYPE_ALPHA);
  SetOptionParamInfo(P_BOX_MODE,"13 Box Size Mode",3,1); addOption(P_BOX_MODE,0,"Auto Size",0); addOption(P_BOX_MODE,1,"Auto Height",1); addOption(P_BOX_MODE,2,"Custom Size",2);
  SetParamInfof(P_BOX_WIDTH,"14 Box Width",FF_TYPE_STANDARD); SetParamInfof(P_BOX_HEIGHT,"15 Box Height",FF_TYPE_STANDARD); SetParamInfof(P_PAD_X,"16 Padding Left Right",FF_TYPE_STANDARD); SetParamInfof(P_PAD_Y,"17 Padding Top Bottom",FF_TYPE_STANDARD);
  SetParamInfof(P_BOX_H,"18 Box Color",FF_TYPE_HUE); SetParamInfof(P_BOX_S,"18 Box Color Sat",FF_TYPE_SATURATION); SetParamInfof(P_BOX_B,"18 Box Color Bri",FF_TYPE_BRIGHTNESS); SetParamInfof(P_BOX_A,"18 Box Color Alpha",FF_TYPE_ALPHA); SetParamInfof(P_RADIUS,"19 Corner Radius",FF_TYPE_STANDARD);
  SetParamInfof(P_BOX_STROKE,"20 Box Stroke",FF_TYPE_BOOLEAN); SetParamInfof(P_BOX_STROKE_WIDTH,"21 Box Stroke Width",FF_TYPE_STANDARD);
  SetParamInfof(P_BOX_STROKE_H,"22 Box Stroke Color",FF_TYPE_HUE); SetParamInfof(P_BOX_STROKE_S,"22 Box Stroke Sat",FF_TYPE_SATURATION); SetParamInfof(P_BOX_STROKE_B,"22 Box Stroke Bri",FF_TYPE_BRIGHTNESS); SetParamInfof(P_BOX_STROKE_A,"22 Box Stroke Alpha",FF_TYPE_ALPHA);
  SetOptionParamInfo(P_BOX_STROKE_POSITION,"23 Stroke Position",3,1); addOption(P_BOX_STROKE_POSITION,0,"Inside",0); addOption(P_BOX_STROKE_POSITION,1,"Center",1); addOption(P_BOX_STROKE_POSITION,2,"Outside",2);
  SetParamInfof(P_BOX_STROKE_OFFSET_X,"24 Stroke Offset X",FF_TYPE_STANDARD); SetParamInfof(P_BOX_STROKE_OFFSET_Y,"25 Stroke Offset Y",FF_TYPE_STANDARD);
  SetParamInfof(P_POS_X,"26 Position X",FF_TYPE_XPOS); SetParamInfof(P_POS_Y,"27 Position Y",FF_TYPE_YPOS); SetParamInfof(P_SCALE,"28 Scale",FF_TYPE_STANDARD);
  SetOptionParamInfo(P_ROTATION,"29 Rotation",4,0); addOption(P_ROTATION,0,"0 degrees",0); addOption(P_ROTATION,1,"90 degrees",1); addOption(P_ROTATION,2,"180 degrees",2); addOption(P_ROTATION,3,"270 degrees",3);
  SetOptionParamInfo(P_MOTION,"30 Motion",7,0); const char* motions[]={"Static","Left","Right","Up Credit","Down Credit","Bounce Horizontal","Bounce Vertical"}; for(unsigned i=0;i<7;++i)addOption(P_MOTION,i,motions[i],float(i));
  SetParamInfof(P_SPEED,"31 Motion Speed",FF_TYPE_STANDARD); SetParamInfof(P_PAUSE,"32 Pause Motion",FF_TYPE_BOOLEAN); SetParamInfof(P_LOOP_GAP,"33 Loop Gap",FF_TYPE_STANDARD); SetParamInfof(P_EDGE_DELAY,"34 Edge Delay",FF_TYPE_STANDARD); SetParamInfof(P_RESET,"35 Reset Position",FF_TYPE_EVENT);
  SetOptionParamInfo(P_FLICKER,"36 Flicker",4,0); addOption(P_FLICKER,0,"Off",0); addOption(P_FLICKER,1,"Sequential",1); addOption(P_FLICKER,2,"Random",2); addOption(P_FLICKER,3,"All",3);
  SetParamInfof(P_FLICKER_SPEED,"37 Flicker Speed",FF_TYPE_STANDARD); SetParamInfof(P_FLICKER_AMOUNT,"38 Flicker Amount",FF_TYPE_STANDARD); SetParamInfof(P_FLICKER_MIN,"39 Flicker Minimum",FF_TYPE_STANDARD);
  SetParamInfof(P_WIGGLE,"40 Wiggle",FF_TYPE_BOOLEAN); SetParamInfof(P_WIGGLE_X,"41 Wiggle X",FF_TYPE_STANDARD); SetParamInfof(P_WIGGLE_Y,"42 Wiggle Y",FF_TYPE_STANDARD); SetParamInfof(P_WIGGLE_ROTATION,"43 Wiggle Rotation",FF_TYPE_STANDARD); SetParamInfof(P_WIGGLE_SPEED,"44 Wiggle Speed",FF_TYPE_STANDARD);
  SetParamInfof(P_ANIM_IN,"45 Animation In",FF_TYPE_BOOLEAN); SetOptionParamInfo(P_IN_AXIS,"46 In Axis",2,0); addOption(P_IN_AXIS,0,"Scroll X",0); addOption(P_IN_AXIS,1,"Scroll Y",1); SetParamInfof(P_IN_INVERT,"47 Invert In",FF_TYPE_BOOLEAN); SetParamInfof(P_IN_DISTANCE,"48 In Distance",FF_TYPE_STANDARD); SetParamInfof(P_IN_DURATION,"49 In Duration",FF_TYPE_STANDARD); SetParamInfof(P_IN_DELAY,"50 In Delay",FF_TYPE_STANDARD);
  SetOptionParamInfo(P_IN_EASE,"51 In Easing",4,3); const char* ease[]={"Linear","Ease In","Ease Out","Ease In Out"}; for(unsigned i=0;i<4;++i)addOption(P_IN_EASE,i,ease[i],float(i));
  SetParamInfof(P_ANIM_OUT,"52 Animation Out",FF_TYPE_BOOLEAN); SetOptionParamInfo(P_OUT_AXIS,"53 Out Axis",2,0); addOption(P_OUT_AXIS,0,"Scroll X",0); addOption(P_OUT_AXIS,1,"Scroll Y",1); SetParamInfof(P_OUT_INVERT,"54 Invert Out",FF_TYPE_BOOLEAN); SetParamInfof(P_OUT_DISTANCE,"55 Out Distance",FF_TYPE_STANDARD); SetParamInfof(P_OUT_DURATION,"56 Out Duration",FF_TYPE_STANDARD); SetParamInfof(P_OUT_DELAY,"57 Out Delay",FF_TYPE_STANDARD);
  SetOptionParamInfo(P_OUT_EASE,"58 Out Easing",4,3); for(unsigned i=0;i<4;++i)addOption(P_OUT_EASE,i,ease[i],float(i)); SetOptionParamInfo(P_OUT_TRIGGER,"59 Out Trigger",2,0); addOption(P_OUT_TRIGGER,0,"Manual",0); addOption(P_OUT_TRIGGER,1,"Auto Timer",1); SetParamInfof(P_HOLD_DURATION,"60 Hold Duration",FF_TYPE_STANDARD); SetParamInfof(P_PLAY_OUT,"61 Play Out",FF_TYPE_EVENT);
  SetParamInfo(P_CREDIT,"99 Effect by Belajar Resolume",FF_TYPE_TEXT,credit_.c_str());
  started_=lastFrame_=std::chrono::steady_clock::now();
}

BRRunningText::~BRRunningText(){ if(gdiplusToken_) GdiplusShutdown(gdiplusToken_); }
void BRRunningText::addOption(unsigned int p,unsigned int e,const char* n,float v){ SetParamElementInfo(p,e,n,v); }

void BRRunningText::enumerateFonts(){
  std::set<std::wstring> names; HDC dc=GetDC(nullptr); LOGFONTW lf{}; lf.lfCharSet=DEFAULT_CHARSET;
  EnumFontFamiliesExW(dc,&lf,reinterpret_cast<FONTENUMPROCW>(enumFontProc),reinterpret_cast<LPARAM>(&names),0); ReleaseDC(nullptr,dc);
  fonts_.clear(); for(const auto& n:names) fonts_.push_back(wideToUtf8(n));
}

FFResult BRRunningText::InitGL(const FFGLViewportStruct* vp){
  viewportWidth_=vp->width; viewportHeight_=vp->height;
  if(!shader_.Compile(vertexShader,fragmentShader)||!quad_.Initialise()) return FF_FAIL;
  rebuildTextTexture(); return CFFGLPlugin::InitGL(vp);
}

void BRRunningText::releaseTextTexture(){ if(textTexture_){ glDeleteTextures(1,&textTexture_); textTexture_=0; } }

void BRRunningText::rebuildTextTexture(){
  textureDirty_=false; releaseTextTexture();
  const float fontPx=18.0f+values_[P_FONT_SIZE]*220.0f;
  std::wstring content=utf8ToWide(text_); if(content.empty()) content=L" ";
  std::wstring familyName=utf8ToWide(fontName_); FontFamily selectedFamily(familyName.c_str()); FontFamily fallbackFamily(L"Arial");
  FontFamily* family=selectedFamily.IsAvailable()?&selectedFamily:&fallbackFamily;
  StringFormat format(StringFormat::GenericTypographic()); format.SetFormatFlags(StringFormatFlagsMeasureTrailingSpaces);
  Bitmap measure(16,16,PixelFormat32bppARGB); Graphics mg(&measure); GraphicsPath path;
  path.AddString(content.c_str(),-1,family,FontStyleRegular,fontPx,PointF(0,0),&format);
  RectF bounds; path.GetBounds(&bounds);
  float stroke=values_[P_TEXT_STROKE]>0.5f ? 1.0f+values_[P_TEXT_STROKE_WIDTH]*18.0f : 0.0f;
  textWidth_=std::max(2u,static_cast<unsigned int>(std::ceil(bounds.Width+stroke*4+8)));
  textHeight_=std::max(2u,static_cast<unsigned int>(std::ceil(bounds.Height+stroke*4+8)));
  textWidth_=std::min(textWidth_,8192u); textHeight_=std::min(textHeight_,8192u);
  Bitmap bmp(textWidth_,textHeight_,PixelFormat32bppARGB); Graphics g(&bmp); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit); g.Clear(Color(0,0,0,0));
  GraphicsPath draw; draw.AddString(content.c_str(),-1,family,FontStyleRegular,fontPx,PointF(4-bounds.X+stroke*2,4-bounds.Y+stroke*2),&format);
  auto hsbaToColor=[&](int h,int s,int b,int a){ float rgb[3]; HSVtoRGB(values_[h],values_[s],values_[b],rgb[0],rgb[1],rgb[2]); return Color(BYTE(clamp01(values_[a])*255),BYTE(clamp01(rgb[0])*255),BYTE(clamp01(rgb[1])*255),BYTE(clamp01(rgb[2])*255)); };
  if(stroke>0){ Pen pen(hsbaToColor(P_TEXT_STROKE_H,P_TEXT_STROKE_S,P_TEXT_STROKE_B,P_TEXT_STROKE_A),stroke); pen.SetLineJoin(LineJoinRound); g.DrawPath(&pen,&draw); }
  SolidBrush fill(hsbaToColor(P_TEXT_H,P_TEXT_S,P_TEXT_B,P_TEXT_A)); g.FillPath(&fill,&draw);
  Rect lockRect(0,0,textWidth_,textHeight_); BitmapData data{}; bmp.LockBits(&lockRect,ImageLockModeRead,PixelFormat32bppARGB,&data);
  glGenTextures(1,&textTexture_); glBindTexture(GL_TEXTURE_2D,textTexture_); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT,4); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,textWidth_,textHeight_,0,GL_BGRA,GL_UNSIGNED_BYTE,data.Scan0); glBindTexture(GL_TEXTURE_2D,0); bmp.UnlockBits(&data);
}

FFResult BRRunningText::ProcessOpenGL(ProcessOpenGLStruct*){
  if(textureDirty_) rebuildTextTexture();
  auto now=std::chrono::steady_clock::now(); double dt=std::chrono::duration<double>(now-lastFrame_).count(); lastFrame_=now; if(!paused_) elapsed_+=std::min(dt,0.1);
  float boxW=values_[P_BOX_WIDTH]*viewportWidth_, boxH=values_[P_BOX_HEIGHT]*viewportHeight_;
  float padX=values_[P_PAD_X]*viewportWidth_*0.25f, padY=values_[P_PAD_Y]*viewportHeight_*0.25f;
  int mode=int(values_[P_BOX_MODE]+0.5f); if(mode==0){boxW=textWidth_+2*padX; boxH=textHeight_+2*padY;} else if(mode==1) boxH=textHeight_+2*padY;
  boxW=std::max(boxW,8.0f); boxH=std::max(boxH,8.0f);
  auto ease=[](float t,int m){t=clamp01(t); if(m==1)return t*t; if(m==2)return 1-(1-t)*(1-t); if(m==3)return t*t*(3-2*t); return t;};
  float inOffsetX=0,inOffsetY=0; double t=elapsed_;
  if(values_[P_ANIM_IN]>0.5f){float delay=values_[P_IN_DELAY]*5, dur=0.05f+values_[P_IN_DURATION]*5; float k=ease(float((t-delay)/dur),int(values_[P_IN_EASE]+.5f)); float dist=values_[P_IN_DISTANCE]*(values_[P_IN_AXIS]<.5f?viewportWidth_:viewportHeight_); float off=(1-k)*dist*(values_[P_IN_INVERT]>.5f?1:-1); if(values_[P_IN_AXIS]<.5f)inOffsetX=off;else inOffsetY=off;}
  bool autoOut=values_[P_OUT_TRIGGER]>.5f; float inEnd=values_[P_IN_DELAY]*5+0.05f+values_[P_IN_DURATION]*5; float outStart=autoOut?inEnd+values_[P_HOLD_DURATION]*20+values_[P_OUT_DELAY]*5:0;
  if(values_[P_ANIM_OUT]>.5f && ((autoOut&&t>=outStart)||manualOut_)){double base=manualOut_?0:outStart; float dur=.05f+values_[P_OUT_DURATION]*5; float k=ease(float((t-base)/dur),int(values_[P_OUT_EASE]+.5f)); float dist=values_[P_OUT_DISTANCE]*(values_[P_OUT_AXIS]<.5f?viewportWidth_:viewportHeight_); float off=k*dist*(values_[P_OUT_INVERT]>.5f?-1:1); if(values_[P_OUT_AXIS]<.5f)inOffsetX=off;else inOffsetY=off;}
  float boxRgb[3], strokeRgb[3]; HSVtoRGB(values_[P_BOX_H],values_[P_BOX_S],values_[P_BOX_B],boxRgb[0],boxRgb[1],boxRgb[2]); HSVtoRGB(values_[P_BOX_STROKE_H],values_[P_BOX_STROKE_S],values_[P_BOX_STROKE_B],strokeRgb[0],strokeRgb[1],strokeRgb[2]);
  ScopedShaderBinding bind(shader_.GetGLID()); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,textTexture_);
  auto u=[&](const char* n){return shader_.FindUniform(n);};
  glUniform1i(u("textTex"),0); glUniform2f(u("resolution"),float(viewportWidth_),float(viewportHeight_)); glUniform2f(u("textSize"),float(textWidth_),float(textHeight_)); glUniform2f(u("boxSize"),boxW,boxH); glUniform2f(u("boxCenter"),values_[P_POS_X]*viewportWidth_,(1-values_[P_POS_Y])*viewportHeight_); glUniform2f(u("paddingPx"),padX,padY);
  glUniform4f(u("boxColor"),boxRgb[0],boxRgb[1],boxRgb[2],values_[P_BOX_A]); glUniform4f(u("boxStrokeColor"),strokeRgb[0],strokeRgb[1],strokeRgb[2],values_[P_BOX_STROKE_A]); glUniform1f(u("radiusPx"),values_[P_RADIUS]*std::min(boxW,boxH)*.5f); glUniform1f(u("boxStrokeWidth"),values_[P_BOX_STROKE_WIDTH]*30); glUniform2f(u("boxStrokeOffset"),(values_[P_BOX_STROKE_OFFSET_X]-.5f)*100,(values_[P_BOX_STROKE_OFFSET_Y]-.5f)*100); glUniform1f(u("boxStrokeEnabled"),values_[P_BOX_STROKE]); glUniform1f(u("strokePosition"),values_[P_BOX_STROKE_POSITION]);
  glUniform1f(u("rotationRad"),values_[P_ROTATION]*1.57079632679f); glUniform1f(u("globalScale"),.25f+values_[P_SCALE]*1.5f); glUniform1f(u("elapsed"),float(elapsed_)); glUniform1f(u("motionMode"),values_[P_MOTION]); glUniform1f(u("motionSpeed"),20+values_[P_SPEED]*600); glUniform1f(u("loopGap"),values_[P_LOOP_GAP]*viewportWidth_*.5f);
  glUniform1f(u("flickerMode"),values_[P_FLICKER]); glUniform1f(u("flickerSpeed"),.2f+values_[P_FLICKER_SPEED]*18); glUniform1f(u("flickerAmount"),values_[P_FLICKER_AMOUNT]); glUniform1f(u("flickerMinimum"),values_[P_FLICKER_MIN]); glUniform1f(u("wiggleEnabled"),values_[P_WIGGLE]); glUniform2f(u("wiggleAmount"),values_[P_WIGGLE_X]*20,values_[P_WIGGLE_Y]*20); glUniform1f(u("wiggleRotation"),values_[P_WIGGLE_ROTATION]); glUniform1f(u("wiggleSpeed"),.2f+values_[P_WIGGLE_SPEED]*15); glUniform2f(u("entranceOffset"),inOffsetX,inOffsetY);
  glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA); quad_.Draw(); glDisable(GL_BLEND); glBindTexture(GL_TEXTURE_2D,0); return FF_SUCCESS;
}

FFResult BRRunningText::DeInitGL(){ releaseTextTexture(); shader_.FreeGLResources(); quad_.Release(); return FF_SUCCESS; }

FFResult BRRunningText::SetFloatParameter(unsigned int i,float v){
  if(i>=P_COUNT||i==P_CREDIT)return FF_FAIL; values_[i]=v;
  if(i==P_FONT_LIST&&!fonts_.empty()){unsigned n=std::min<unsigned>(unsigned(v+.5f),unsigned(fonts_.size()-1));fontName_=fonts_[n];textureDirty_=true;RaiseParamEvent(P_FONT_NAME,FF_EVENT_FLAG_VALUE);}
  if(i==P_REFRESH_FONTS&&v!=0){enumerateFonts();}
  if(i==P_RESET&&v!=0){elapsed_=0;manualOut_=false;}
  if(i==P_PLAY_OUT&&v!=0){manualOut_=true;elapsed_=0;}
  if(i==P_PAUSE)paused_=v>.5f;
  if((i>=P_FONT_SIZE&&i<=P_TEXT_STROKE_A)||i==P_FONT_LIST)textureDirty_=true;
  return FF_SUCCESS;
}
float BRRunningText::GetFloatParameter(unsigned int i){return i<P_COUNT?values_[i]:0;}
FFResult BRRunningText::SetTextParameter(unsigned int i,const char* v){if(i==P_TEXT){text_=v?v:"";textureDirty_=true;return FF_SUCCESS;}if(i==P_FONT_NAME){fontName_=v?v:"Arial";textureDirty_=true;return FF_SUCCESS;}return FF_FAIL;}
char* BRRunningText::GetTextParameter(unsigned int i){static char buffer[8192];const std::string* s=i==P_TEXT?&text_:i==P_FONT_NAME?&fontName_:&credit_;std::strncpy(buffer,s->c_str(),sizeof(buffer)-1);buffer[sizeof(buffer)-1]=0;return buffer;}
