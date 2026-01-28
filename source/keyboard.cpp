#include "keyboard.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <switch.h>

namespace tsl {

    int g_argc;
    char** g_argv;

    // --- KeyboardFrame ---
    // Custom OverlayFrame to allow wider layout for the keyboard
    class KeyboardFrame : public elm::OverlayFrame {
    public:
        KeyboardFrame(const std::string& title, const std::string& subtitle) : elm::OverlayFrame(title, subtitle) {}
        
        virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
            setBoundaries(parentX, parentY, parentWidth, parentHeight);
            
            if (m_contentElement != nullptr) {
                // Reduced padding: Left 25 (vs 35), Right 25 (vs 85-35=50) -> Width - 50 total padding
                // Original: parentX + 35, width - 85
                // New: parentX + 25, width - 50
                m_contentElement->setBoundaries(parentX + 25, parentY + 115, parentWidth - 50, parentHeight - 73 - 110);
                m_contentElement->layout(parentX + 25, parentY + 115, parentWidth - 50, parentHeight - 73 - 110);
                m_contentElement->invalidate();
            }
        }
    };

    // --- KeyboardButton ---
    class KeyboardButton : public elm::Element {
    public:
        KeyboardButton(char c, std::function<void(char)> onClick) 
            : m_char(c), m_label(1, c), m_onClick(onClick) {
            m_isItem = true;
        }
        
        KeyboardButton(const std::string& label, std::function<void()> onClickAction) 
            : m_char(0), m_label(label), m_onClickAction(onClickAction) {
            m_isItem = true;
        }

        virtual s32 getHeight() override { return 60; }

        virtual void draw(gfx::Renderer *renderer) override {
            auto color = m_focused ? style::color::ColorHighlight : style::color::ColorText;
            if (m_focused) {
                renderer->drawRoundedRect(this->getX(), this->getY(), this->getWidth(), this->getHeight(), 8.0f, a(style::color::ColorClickAnimation));
            }
            renderer->drawRect(this->getX(), this->getY(), this->getWidth(), this->getHeight(), a(style::color::ColorFrame));
            
            // Center text approximately (standard Tesla font width is ~10-15px for size 25)
            s32 textX = this->getX() + (this->getWidth() / 2) - (m_label.length() * 8);
            s32 textY = this->getY() + (this->getHeight() / 2) + 12;
            renderer->drawString(m_label.c_str(), false, textX, textY, 25, a(color));
        }

        virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
            setBoundaries(parentX, parentY, parentWidth, parentHeight);
        }

        virtual inline Element* requestFocus(Element *oldFocus, FocusDirection direction) override {
            return this;
        }

        virtual inline bool onClick(u64 keys) override {
            if (keys & KEY_A) {
                if (m_onClick) m_onClick(m_char);
                else if (m_onClickAction) m_onClickAction();
                return true;
            }
            return false;
        }

        // virtual void setFocused(bool focused) override {
        //     elm::Element::setFocused(focused); // Call base implementation
        //     if (this->getParent()) {
        //         this->getParent()->setFocused(focused); // Propagate to KeyboardRow
        //     }
        // }

    private:
        char m_char;
        std::string m_label;
        std::function<void(char)> m_onClick;
        std::function<void()> m_onClickAction;
    };

    // --- KeyboardRow ---
    class KeyboardRow : public elm::Element {
    public:
        KeyboardRow() { m_isItem = true; }
        virtual ~KeyboardRow() {
            for (auto* btn : m_buttons) delete btn;
        }

        void addButton(KeyboardButton* btn) {
            btn->setParent(this);
            m_buttons.push_back(btn);
        }

        // Match button height to remove vertical gap
        virtual s32 getHeight() override { return 60; }

        virtual void draw(gfx::Renderer *renderer) override {
            // Force layout update because List doesn't call layout() on children
            this->layout(this->getX(), this->getY(), this->getWidth(), this->getHeight());
            
            for (auto* btn : m_buttons) btn->frame(renderer);
        }

        virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
            // Do NOT reset boundaries here. Trust the parent List to set X, Y, W, H.
            // Just layout the children (buttons) within the current bounds.

            if (m_buttons.empty()) return;

            // Use standard clean division layout - "Normal way"
            u16 btnWidth = this->getWidth() / m_buttons.size();
            u16 btnHeight = 60; // Keep buttons 60px tall
            u16 yOffset = (this->getHeight() - btnHeight) / 2; // Will be 0 if height is 60

            for (size_t i = 0; i < m_buttons.size(); ++i) {
                // Determine width for this button (handle remainders on last button)
                u16 width = (i == m_buttons.size() - 1) ? (this->getWidth() - i * btnWidth) : btnWidth;
                m_buttons[i]->layout(parentX + i * btnWidth, parentY + yOffset, width, btnHeight);
            }
        }

        virtual inline Element* requestFocus(Element *oldFocus, FocusDirection direction) override {
            if (m_buttons.empty()) return nullptr;
            
            if (oldFocus && (direction == FocusDirection::Up || direction == FocusDirection::Down)) {
                s32 targetX = oldFocus->getX() + (oldFocus->getWidth() / 2);
                auto it = std::min_element(m_buttons.begin(), m_buttons.end(), [targetX](Element* a, Element* b) {
                    return std::abs(a->getX() + (s32)(a->getWidth() / 2) - targetX) < std::abs(b->getX() + (s32)(b->getWidth() / 2) - targetX);
                });
                return (*it)->requestFocus(oldFocus, direction);
            }

            // Sequential horizontal navigation within the same row
            if (oldFocus && (direction == FocusDirection::Left || direction == FocusDirection::Right)) {
                for (size_t i = 0; i < m_buttons.size(); ++i) {
                    if (m_buttons[i] == oldFocus) {
                        if (direction == FocusDirection::Left) {
                            if (i > 0) return m_buttons[i-1]->requestFocus(oldFocus, direction);
                        } else {
                            if (i < m_buttons.size() - 1) return m_buttons[i+1]->requestFocus(oldFocus, direction);
                        }
                        return oldFocus; // Stay on the boundary button
                    }
                }
            }
            
            if (direction == FocusDirection::Left) return m_buttons.back()->requestFocus(oldFocus, direction);
            return m_buttons.front()->requestFocus(oldFocus, direction);
        }

        // virtual inline bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
        //     for (size_t i = 0; i < m_buttons.size(); ++i) {
        //         if (m_buttons[i]->hasFocus()) {
        //             if (keysDown & KEY_DLEFT) {
        //                 if (i > 0) {
        //                     tsl::Overlay::get()->getCurrentGui()->requestFocus(m_buttons[i-1], FocusDirection::Left);
        //                     return true;
        //                 }
        //             } else if (keysDown & KEY_DRIGHT) {
        //                 if (i < m_buttons.size() - 1) {
        //                     tsl::Overlay::get()->getCurrentGui()->requestFocus(m_buttons[i+1], FocusDirection::Right);
        //                     return true;
        //                 }
        //             }
        //         }
        //     }
        //     return false;
        // }

    private:
        std::vector<KeyboardButton*> m_buttons;
    };

    // --- ValueDisplay ---
    class ValueDisplay : public elm::Element {
    public:
        ValueDisplay(KeyboardGui* gui, const std::string& title, std::string& value, size_t& cursorPos) 
            : m_gui(gui), m_title(title), m_value(value), m_cursorPos(cursorPos) {
            m_isItem = true;
        }

        virtual s32 getHeight() override { return 70; }

        virtual void draw(gfx::Renderer *renderer) override {
            std::string val;
            size_t pos;
            {
                std::lock_guard<std::recursive_mutex> lock(m_gui->getMutex());
                val = m_value;
                pos = m_cursorPos;
            }

            // Draw background for the value area
            renderer->drawRect(this->getX(), this->getY(), this->getWidth(), this->getHeight(), a(style::color::ColorFrame));
            
            std::string displayTitle = m_title + ": ";
            s32 titleWidth = renderer->getTextDimensions(displayTitle.c_str(), false, 25).first;
            s32 textY = this->getY() + (this->getHeight() / 2) + 12;
            
            renderer->drawString(displayTitle.c_str(), false, this->getX() + 15, textY, 25, a(style::color::ColorText));

            // Draw current value
            renderer->drawString(val.c_str(), false, this->getX() + 15 + titleWidth, textY, 25, a(style::color::ColorText));

            // Draw cursor with accurate position
            s32 prefixWidth = renderer->getTextDimensions(val.substr(0, pos).c_str(), false, 25).first;
            s32 cursorX = this->getX() + 15 + titleWidth + prefixWidth;
            renderer->drawRect(cursorX, this->getY() + 15, 2, 40, a(style::color::ColorHighlight));
        }

        virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
            setBoundaries(parentX, parentY, parentWidth, parentHeight);
        }

        virtual inline Element* requestFocus(Element *oldFocus, FocusDirection direction) override {
            return nullptr;
        }

    private:
        KeyboardGui* m_gui;
        std::string m_title;
        std::string& m_value;
        size_t& m_cursorPos;
    };

    // --- KeyboardGui ---
    KeyboardGui::KeyboardGui(searchType_t type, const std::string& initialValue, const std::string& title)
        : m_type(type), m_value(initialValue), m_title(title) {
        m_isNumpad = (type != SEARCH_TYPE_POINTER && type != SEARCH_TYPE_NONE); 
        m_cursorPos = m_value.length();
        tsl::disableJumpTo = true;
    }

    KeyboardGui::~KeyboardGui() {
        tsl::disableJumpTo = false;
    }

    elm::Element* KeyboardGui::createUI() {
        auto* frame = new KeyboardFrame(m_title, ""); // Subtitle removed to avoid squeezing
        auto* list = new elm::List();
        
        // Add a dedicated item to show the current value
        auto* valItem = new ValueDisplay(this, "Value", m_value, m_cursorPos);
        m_valueDisplay = valItem;
        list->addItem(valItem);

        auto keyPress = [this](char c) { this->handleKeyPress(c); };
        
        if (m_type == SEARCH_TYPE_NONE) {
            auto* typeRow1 = new KeyboardRow();
            typeRow1->addButton(new KeyboardButton("U8", [this]{ this->switchType(SEARCH_TYPE_UNSIGNED_8BIT); }));
            typeRow1->addButton(new KeyboardButton("U16", [this]{ this->switchType(SEARCH_TYPE_UNSIGNED_16BIT); }));
            typeRow1->addButton(new KeyboardButton("U32", [this]{ this->switchType(SEARCH_TYPE_UNSIGNED_32BIT); }));
            typeRow1->addButton(new KeyboardButton("U64", [this]{ this->switchType(SEARCH_TYPE_UNSIGNED_64BIT); }));
            list->addItem(typeRow1);

            auto* typeRow2 = new KeyboardRow();
            typeRow2->addButton(new KeyboardButton("FLOAT", [this]{ this->switchType(SEARCH_TYPE_FLOAT); }));
            typeRow2->addButton(new KeyboardButton("DOUBLE", [this]{ this->switchType(SEARCH_TYPE_DOUBLE); }));
            typeRow2->addButton(new KeyboardButton("POINTER", [this]{ this->switchType(SEARCH_TYPE_POINTER); }));
            list->addItem(typeRow2);
        }

        if (m_isNumpad) {
            auto* row1 = new KeyboardRow();
            row1->addButton(new KeyboardButton('1', keyPress));
            row1->addButton(new KeyboardButton('2', keyPress));
            row1->addButton(new KeyboardButton('3', keyPress));
            list->addItem(row1);

            auto* row2 = new KeyboardRow();
            row2->addButton(new KeyboardButton('4', keyPress));
            row2->addButton(new KeyboardButton('5', keyPress));
            row2->addButton(new KeyboardButton('6', keyPress));
            list->addItem(row2);

            auto* row3 = new KeyboardRow();
            row3->addButton(new KeyboardButton('7', keyPress));
            row3->addButton(new KeyboardButton('8', keyPress));
            row3->addButton(new KeyboardButton('9', keyPress));
            list->addItem(row3);

            auto* row4 = new KeyboardRow();
            row4->addButton(new KeyboardButton("BS", [this]{ this->handleBackspace(); }));
            row4->addButton(new KeyboardButton('0', keyPress));
            row4->addButton(new KeyboardButton("OK", [this]{ this->handleConfirm(); }));
            list->addItem(row4);
        } else {
            auto* row1 = new KeyboardRow();
            for (char c : std::string("1234567890")) row1->addButton(new KeyboardButton(c, keyPress));
            list->addItem(row1);

            auto* row2 = new KeyboardRow();
            for (char c : std::string("QWERTYUIOP")) row2->addButton(new KeyboardButton(c, keyPress));
            list->addItem(row2);
            
            auto* row3 = new KeyboardRow();
            for (char c : std::string("ASDFGHJKL")) row3->addButton(new KeyboardButton(c, keyPress));
            list->addItem(row3);

            auto* row4 = new KeyboardRow();
            row4->addButton(new KeyboardButton("BS", [this]{ this->handleBackspace(); }));
            for (char c : std::string("ZXCVBNM")) row4->addButton(new KeyboardButton(c, keyPress));
            row4->addButton(new KeyboardButton("OK", [this]{ this->handleConfirm(); }));
            list->addItem(row4);
        }

        frame->setContent(list);
        return frame;
    }

    void KeyboardGui::update() {}

    bool KeyboardGui::handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState leftJoyStick, HidAnalogStickState rightJoyStick) {
        // Shoulder buttons for cursor movement
        if (keysDown & KEY_L) {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (m_cursorPos > 0) m_cursorPos--;
            return true;
        }
        if (keysDown & KEY_R) {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (m_cursorPos < m_value.length()) m_cursorPos++;
            return true;
        }
        if (keysHeld & (KEY_L | KEY_R)) return true;

        // Nintendo-style shortcuts
        if (keysDown & KEY_B) {
            handleBackspace();
            return true;
        }
        if (keysDown & KEY_X) {
            handleCancel();
            return true;
        }
        if (keysDown & KEY_PLUS) {
            handleConfirm();
            return true;
        }
        if (keysDown & KEY_Y && !m_isNumpad) {
            handleKeyPress(' ');
            return true;
        }
        return false;
    }

    void KeyboardGui::handleKeyPress(char c) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_value.insert(m_cursorPos, 1, c);
        m_cursorPos++;
    }

    void KeyboardGui::handleBackspace() {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_cursorPos > 0) {
            m_value.erase(m_cursorPos - 1, 1);
            m_cursorPos--;
        }
    }

    void KeyboardGui::handleConfirm() {
        saveAndExit();
    }

    void KeyboardGui::handleCancel() {
        tsl::setNextOverlay("sdmc:/switch/.overlays/breezehand.ovl", "--direct");
        tsl::Overlay::get()->close();
    }

    void KeyboardGui::switchType(searchType_t newType) {
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_type = newType;
            m_isNumpad = (newType != SEARCH_TYPE_POINTER);
        }
        // Better to swapTo the same Gui with new params to refresh layout properly
        tsl::swapTo<KeyboardGui>(m_type, m_value, m_title);
    }

    void KeyboardGui::saveAndExit() {
        std::string val;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            val = m_value;
        }
        std::ofstream f("sdmc:/switch/.overlays/breezehand_kb.bin", std::ios::binary);
        if (f.is_open()) {
            f.write(val.c_str(), val.size() + 1);
        }
        f.close();

        tsl::setNextOverlay("sdmc:/switch/.overlays/breezehand.ovl", "--direct");
        tsl::Overlay::get()->close();
    }

    // --- KeyboardOverlay ---
    void KeyboardOverlay::initServices() {
        // Force layout logic to match the visible 448px sidebar
        // This prevents OverlayFrame from laying out buttons across 1280px
        tsl::cfg::FramebufferWidth = 448;
        tsl::cfg::LayerWidth = 448;
        tsl::cfg::LayerPosX = 1280 - 448;

        // Parse arguments
        m_type = SEARCH_TYPE_NONE;
        m_value = "";
        m_title = "Keyboard";

        for (int i = 1; i < g_argc; i++) {
            if (strcmp(g_argv[i], "--type") == 0 && i + 1 < g_argc) {
                char* end;
                long val = std::strtol(g_argv[++i], &end, 10);
                if (end != g_argv[i]) {
                    m_type = (searchType_t)val;
                } else {
                    m_type = SEARCH_TYPE_NONE;
                }
            } else if (strcmp(g_argv[i], "--value") == 0 && i + 1 < g_argc) {
                m_value = g_argv[++i];
            } else if (strcmp(g_argv[i], "--title") == 0 && i + 1 < g_argc) {
                m_title = g_argv[++i];
            }
        }
    }

    void KeyboardOverlay::exitServices() {}
    
    void KeyboardOverlay::onShow() {}
    void KeyboardOverlay::onHide() {}

    std::unique_ptr<Gui> KeyboardOverlay::loadInitialGui() {
        return std::make_unique<KeyboardGui>(m_type, m_value, m_title);
    }

}
