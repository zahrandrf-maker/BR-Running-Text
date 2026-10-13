#pragma once

#include <FFGLSDK.h>
#include <chrono>
#include <string>
#include <vector>

class BRRunningText final : public CFFGLPlugin {
public:
  BRRunningText();
  ~BRRunningText() override;

  FFResult InitGL(const FFGLViewportStruct* vp) override;
  FFResult ProcessOpenGL(ProcessOpenGLStruct* pGL) override;
  FFResult DeInitGL() override;
  FFResult SetFloatParameter(unsigned int index, float value) override;
  float GetFloatParameter(unsigned int index) override;
  FFResult SetTextParameter(unsigned int index, const char* value) override;
  char* GetTextParameter(unsigned int index) override;

  static LRESULT CALLBACK EditorWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

private:
  void enumerateFonts();
  void rebuildTextTexture();
  void releaseTextTexture();
  void addOption(unsigned int parameter, unsigned int element, const char* name, float value);
  void openTextEditor();

  std::string text_ = "Belajar Resolume";
  std::string fontName_ = "Arial";
  std::string credit_ = "Effect by Belajar Resolume";
  std::vector<std::string> fonts_;
  std::vector<float> values_;

  unsigned int viewportWidth_ = 1920;
  unsigned int viewportHeight_ = 1080;
  unsigned int textWidth_ = 1;
  unsigned int textHeight_ = 1;
  unsigned int textTexture_ = 0;
  bool textureDirty_ = true;

  unsigned long long gdiplusToken_ = 0;
  std::chrono::steady_clock::time_point started_;
  std::chrono::steady_clock::time_point lastFrame_;
  double elapsed_ = 0.0;
  bool paused_ = false;
  bool manualOut_ = false;
  bool visible_ = true;
  HWND editorWindow_ = nullptr;
  HWND editorTextBox_ = nullptr;

  ffglex::FFGLShader shader_;
  ffglex::FFGLScreenQuad quad_;
};
