/**
 * XtcReaderChapterSelectionActivity.h
 *
 * Chapter selection activity for XTC reader
 * Displays list of chapters and allows navigation to specific chapter
 */

#pragma once

#include <Xtc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <memory>

#include "../Activity.h"

class XtcReaderChapterSelectionActivity final : public Activity {
  std::shared_ptr<Xtc> xtc;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  uint32_t currentPage = 0;
  int selectorIndex = 0;
  bool updateRequired = false;
  bool waitForButtonRelease = true;
  const std::function<void()> onGoBack;
  const std::function<void(uint32_t newPage)> onSelectPage;

  // Number of items that fit on a page
  int getPageItems() const;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void renderScreen();

 public:
  explicit XtcReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const std::shared_ptr<Xtc>& xtc, const uint32_t currentPage,
                                             const std::function<void()>& onGoBack,
                                             const std::function<void(uint32_t newPage)>& onSelectPage)
      : Activity("XtcReaderChapterSelection", renderer, mappedInput),
        xtc(xtc),
        currentPage(currentPage),
        onGoBack(onGoBack),
        onSelectPage(onSelectPage) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
