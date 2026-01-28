#pragma once

#include <tesla.hpp>
#include "search_types.hpp"
#include <string>
#include <vector>
#include <mutex>

namespace tsl {

    extern int g_argc;
    extern char** g_argv;

    class KeyboardGui : public Gui {
    public:
        KeyboardGui(searchType_t type, const std::string& initialValue, const std::string& title);
        virtual ~KeyboardGui();

        // Gui overrides
        virtual elm::Element* createUI() override;
        virtual void update() override;
        virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState leftJoyStick, HidAnalogStickState rightJoyStick) override;
 
        std::recursive_mutex& getMutex() { return m_mutex; }

    private:
        searchType_t m_type;
        std::string m_value;
        std::string m_title;
        size_t m_cursorPos = 0;
        bool m_isNumpad;
        elm::Element* m_valueDisplay = nullptr;
        std::recursive_mutex m_mutex;
        
        void handleKeyPress(char c);
        void handleBackspace();
        void handleConfirm();
        void handleCancel();
        void switchType(searchType_t newType);
        
        void saveAndExit();
    };

    class KeyboardOverlay : public Overlay {
    public:
        KeyboardOverlay() : m_type(SEARCH_TYPE_NONE) {}
        virtual ~KeyboardOverlay() {}

        // Overlay overrides
        virtual void initServices() override;
        virtual void exitServices() override;
        virtual void onShow() override;
        virtual void onHide() override;
        virtual std::unique_ptr<Gui> loadInitialGui() override;

    private:
        searchType_t m_type;
        std::string m_value;
        std::string m_title;
    };

}
