/**
 * XtcReaderChapterSelectionActivity.cpp
 *
 * Chapter selection activity implementation for XTC reader
 */

#include "XtcReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "config.h"

namespace {
// Time threshold for treating a long press as a page-up/page-down
constexpr int SKIP_PAGE_MS = 700;
}  // namespace

int XtcReaderChapterSelectionActivity::getPageItems() const {
  // Layout constants used in renderScreen
  constexpr int startY = 60;
  constexpr int lineHeight = 30;

  const int screenHeight = renderer.getScreenHeight();
  const int availableHeight = screenHeight - startY;
  int items = availableHeight / lineHeight;

  // Ensure we always have at least one item per page
  if (items < 1) {
    items = 1;
  }
  return items;
}

void XtcReaderChapterSelectionActivity::taskTrampoline(void* param) {
  auto* self = static_cast<XtcReaderChapterSelectionActivity*>(param);
  self->displayTaskLoop();
}

void XtcReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();

  if (!xtc || !xtc->hasChapters()) {
    return;
  }

  renderingMutex = xSemaphoreCreateMutex();

  // Find chapter index for current page
  int currentChapterIndex = xtc->getChapterIndexForPage(currentPage);
  selectorIndex = (currentChapterIndex >= 0) ? currentChapterIndex : 0;

  // Trigger first update
  updateRequired = true;
  xTaskCreate(&XtcReaderChapterSelectionActivity::taskTrampoline, "XtcChapterSelTask",
              4096,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void XtcReaderChapterSelectionActivity::onExit() {
  Activity::onExit();

  // Wait until not rendering to delete task
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void XtcReaderChapterSelectionActivity::loop() {
  // Wait until the confirm button is fully released before accepting input
  if (waitForButtonRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      waitForButtonRelease = false;
    }
    return;
  }

  const auto& chapters = xtc->getChapters();
  if (chapters.empty()) {
    return;
  }

  const int chapterCount = static_cast<int>(chapters.size());

  const bool prevReleased = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Left);
  const bool nextReleased = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Right);

  const bool skipPage = mappedInput.getHeldTime() > SKIP_PAGE_MS;
  const int pageItems = getPageItems();

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const auto& selectedChapter = chapters[selectorIndex];
    // startPage가 1-based로 저장된 파일을 고려하여 조정
    // (XTC_FORMAT.md는 0-based를 명시하지만, 일부 컨버터는 1-based 사용)
    const uint32_t targetPage = (selectedChapter.startPage > 0) ? selectedChapter.startPage - 1 : 0;
    onSelectPage(targetPage);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoBack();
  } else if (prevReleased) {
    if (skipPage) {
      selectorIndex = ((selectorIndex / pageItems - 1) * pageItems + chapterCount) % chapterCount;
    } else {
      selectorIndex = (selectorIndex + chapterCount - 1) % chapterCount;
    }
    updateRequired = true;
  } else if (nextReleased) {
    if (skipPage) {
      selectorIndex = ((selectorIndex / pageItems + 1) * pageItems) % chapterCount;
    } else {
      selectorIndex = (selectorIndex + 1) % chapterCount;
    }
    updateRequired = true;
  }
}

void XtcReaderChapterSelectionActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      renderScreen();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void XtcReaderChapterSelectionActivity::renderScreen() {
  renderer.clearScreen();

  const auto& chapters = xtc->getChapters();
  if (chapters.empty()) {
    renderer.drawCenteredText(READER_FONT_ID, 300, "No chapters", true, BOLD);
    renderer.displayBuffer();
    return;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const int pageItems = getPageItems();
  const int chapterCount = static_cast<int>(chapters.size());

  renderer.drawCenteredText(READER_FONT_ID, 10, "Select Chapter", true, BOLD);

  const auto pageStartIndex = selectorIndex / pageItems * pageItems;
  renderer.fillRect(0, 60 + (selectorIndex % pageItems) * 30 - 2, pageWidth - 1, 30);

  for (int i = pageStartIndex; i < chapterCount && i < pageStartIndex + pageItems; i++) {
    const auto& chapter = chapters[i];
    std::string displayText = chapter.name.empty() ? "Unnamed" : chapter.name;

    // Truncate long chapter names (same logic as EpubReaderChapterSelectionActivity)
    const int maxTextWidth = pageWidth - 40;
    int textWidth = renderer.getTextWidth(UI_FONT_ID, displayText.c_str());
    while (textWidth > maxTextWidth && displayText.length() > 11) {
      displayText.replace(displayText.length() - 8, 8, "...");
      textWidth = renderer.getTextWidth(UI_FONT_ID, displayText.c_str());
    }

    renderer.drawText(UI_FONT_ID, 20, 60 + (i % pageItems) * 30, displayText.c_str(), i != selectorIndex);
  }

  renderer.displayBuffer();
}
