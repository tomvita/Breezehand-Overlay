#pragma once

#include <tesla.hpp>
#include "search_types.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <functional>

namespace tsl {

    class KeyboardGui : public Gui {
    public:
        KeyboardGui(searchType_t type, const std::string& initialValue, const std::string& title, 
                    std::function<void(std::string)> onComplete,
                    std::function<std::string(std::string&, size_t&)> onNoteUpdate = nullptr,
                    bool constrained = true,
                    std::function<std::string(std::string&, size_t&)> onGetSignedEditValue = nullptr,
                    std::function<std::string(std::string&, size_t&)> onGetUnsignedEditValue = nullptr,
                    std::function<std::string(std::string&, size_t&)> onGetFloatEditValue = nullptr,
                    std::function<bool(std::string&, size_t&, const std::string&)> onApplySignedEdit = nullptr,
                    std::function<bool(std::string&, size_t&, const std::string&)> onApplyUnsignedEdit = nullptr,
                    std::function<bool(std::string&, size_t&, const std::string&)> onApplyFloatEdit = nullptr,
                    std::function<std::string(std::string&, size_t&)> onGetAsmEditValue = nullptr,
                    std::function<bool(std::string&, size_t&, const std::string&)> onApplyAsmEdit = nullptr,
                    std::function<bool(std::string&, size_t&)> onClearStoredValue = nullptr,
                    std::function<u32(std::string&, size_t&)> onGetCodeType = nullptr,
                    std::function<bool(std::string&, size_t&, u32, u64)> onApplyComboType = nullptr,
                    std::function<bool(std::string&, size_t&, u32)> onSetComboCodeType = nullptr,
                    std::function<bool(std::string&, size_t&)> onToggleC4AutoRepeat = nullptr);
        virtual ~KeyboardGui();

        // Gui overrides
        virtual elm::Element* createUI() override;
        virtual void update() override;
        virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState leftJoyStick, HidAnalogStickState rightJoyStick) override;
 
        std::recursive_mutex& getMutex() { return m_mutex; }
        
        // Overtype Mode Check
        bool isOvertypeMode() const;
        
        // Expose m_manualOvertype for Lambda access in createUI
        void toggleManualOvertype() { m_manualOvertype = !m_manualOvertype; }

    private:
        searchType_t m_type;
        std::string m_value;
        std::string m_title;
        std::function<void(std::string)> m_onComplete;
        std::function<std::string(std::string&, size_t&)> m_onNoteUpdate;
        std::function<std::string(std::string&, size_t&)> m_onGetSignedEditValue;
        std::function<std::string(std::string&, size_t&)> m_onGetUnsignedEditValue;
        std::function<std::string(std::string&, size_t&)> m_onGetFloatEditValue;
        std::function<bool(std::string&, size_t&, const std::string&)> m_onApplySignedEdit;
        std::function<bool(std::string&, size_t&, const std::string&)> m_onApplyUnsignedEdit;
        std::function<bool(std::string&, size_t&, const std::string&)> m_onApplyFloatEdit;
        std::function<std::string(std::string&, size_t&)> m_onGetAsmEditValue;
        std::function<bool(std::string&, size_t&, const std::string&)> m_onApplyAsmEdit;
        std::function<bool(std::string&, size_t&)> m_onClearStoredValue;
        std::function<u32(std::string&, size_t&)> m_onGetCodeType;
        std::function<bool(std::string&, size_t&, u32, u64)> m_onApplyComboType;
        std::function<bool(std::string&, size_t&, u32)> m_onSetComboCodeType;
        std::function<bool(std::string&, size_t&)> m_onToggleC4AutoRepeat;
        size_t m_cursorPos = 0;
        bool m_isNumpad;
        elm::Element* m_valueDisplay = nullptr;
        elm::OverlayFrame* m_frame = nullptr;
        std::recursive_mutex m_mutex;
        
        void handleKeyPress(char c, bool directPhysicalInput = false);
        void handleBackspace();
        void handleConfirm();
        void handleCancel();
        void switchType(searchType_t newType);

    private:
        bool m_manualOvertype = false;
        bool m_isConstrained = true;
        bool m_capsMode = true;
        std::function<void()> m_onToggleCapsVisual;
        bool m_comboCaptureActive = false;
        u32 m_comboCaptureTargetType = 0;
        u64 m_comboCaptureArmedTick = 0;
        u64 m_comboCaptureStartTick = 0;
        u64 m_comboCapturedKeys = 0;
        HidKeyboardState m_prevKeyboardState{};
        bool m_hasPrevKeyboardState = false;

        bool handlePhysicalKeyboardInput();
    };

}
