#pragma once

#include "pajlada/settings/equal.hpp"
#include "pajlada/settings/internal.hpp"
#include "pajlada/settings/serialize.hpp"
#include "pajlada/settings/signalargs.hpp"

#include <rapidjson/document.h>
#include <rapidjson/pointer.h>
#include <pajlada/signals/signal.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace pajlada {
namespace Settings {

enum class SettingOption : uint64_t {
    DoNotWriteToJSON = (1ull << 1ull),

    ForceSetOptions = (1ull << 2ull),

    SaveInitialValue = (1ull << 3ull),

    /// A remote setting is a setting that is never saved locally, nor registered locally with any callbacks or anything
    Remote = (1ull << 4ull),

    Default = 0,
};

inline SettingOption
operator|(const SettingOption &lhs, const SettingOption &rhs)
{
    return static_cast<SettingOption>(
        (static_cast<uint64_t>(lhs) | static_cast<uint64_t>(rhs)));
}

inline SettingOption operator&(const SettingOption &lhs,
                               const SettingOption &rhs)
{
    return static_cast<SettingOption>(
        (static_cast<uint64_t>(lhs) & static_cast<uint64_t>(rhs)));
}

class ISettingData
{
public:
    ISettingData() = default;

    virtual ~ISettingData() = default;

    SettingOption options = SettingOption::Default;

    inline bool
    optionEnabled(SettingOption option) const
    {
        return (this->options & option) == option;
    }

    void marshal(rapidjson::Document &d);

    virtual rapidjson::Value marshalInto(rapidjson::Document &d) = 0;
    virtual bool unmarshalFrom(rapidjson::Document &d) = 0;
    virtual bool unmarshalValue(const rapidjson::Value &fromValue) = 0;

    virtual void registerDocument(rapidjson::Document &d) = 0;

    const std::string &getPath() const;

    bool hasBeenSet() const;

    void setPath(const std::string &_path);

    Signals::Signal<const SignalArgs &> simpleValueChanged;

protected:
    // Setting path (i.e. /a/b/c/3/d/e)
    std::string path;

    rapidjson::Value *get(rapidjson::Document &document);

    // valueHasBeenSet is set to true when setValue is used,
    // except for when it's set via resetToDefaultValue
    bool valueHasBeenSet = false;
};

template <typename Type>
class SettingData : public ISettingData,
                    public std::enable_shared_from_this<SettingData<Type>>
{
    SettingData()
        : ISettingData()
        , defaultValue(Type())
        , value(Type())
    {
    }

    SettingData(const Type &_defaultValue)
        : ISettingData()
        , defaultValue(_defaultValue)
        , value(_defaultValue)
    {
    }

    SettingData(const Type &_defaultValue, const Type &_currentValue)
        : ISettingData()
        , defaultValue(_defaultValue)
        , value(_currentValue)
    {
    }

    SettingData(Type &&_defaultValue)
        : ISettingData()
        , defaultValue(_defaultValue)
        , value(_defaultValue)
    {
    }

    SettingData(Type &&_defaultValue, Type &&_currentValue)
        : ISettingData()
        , defaultValue(std::move(_defaultValue))
        , value(std::move(_currentValue))
    {
    }

public:
    using valueChangedCallbackType =
        std::function<void(const Type &, const SignalArgs &args)>;

    rapidjson::Value
    marshalInto(rapidjson::Document &d) override
    {
        return Serialize<Type>::get(this->getValue(), d.GetAllocator());
    }

    bool
    unmarshalFrom(rapidjson::Document &document) override
    {
        auto valuePointer = this->get(document);
        if (valuePointer == nullptr) {
            return false;
        }

        auto newValue = Deserialize<Type>::get(*valuePointer);

        SignalArgs args;

        args.source = SignalArgs::Source::Unmarshal;
        args.path = this->getPath();

        this->setValue(newValue, std::move(args));

        return true;
    }

    bool
    unmarshalValue(const rapidjson::Value &fromValue) override
    {
        auto newValue = Deserialize<Type>::get(fromValue);

        SignalArgs args;

        args.source = SignalArgs::Source::Unmarshal;
        args.path = this->getPath();

        this->setValue(newValue, std::move(args));

        return true;
    }

    void
    registerDocument(rapidjson::Document &d) override
    {
        // PS_DEBUG("[" << this->path << "] Register document");

        this->valueChanged.connect([this, &d](const Type &, const auto &) {
            this->marshal(d);  //
        });

        if (this->optionEnabled(SettingOption::SaveInitialValue)) {
            this->marshal(d);
        }
    }

    void
    setValue(const Type &newValue, SignalArgs &&args)
    {
        if (IsEqual<Type>::get(this->value, newValue)) {
            return;
        }

        this->valueHasBeenSet = true;
        this->value = newValue;

        SignalArgs invocationArgs(args);

        invocationArgs.path = this->path;

        if (invocationArgs.source == SignalArgs::Source::Unset) {
            invocationArgs.source = SignalArgs::Source::Setter;
        }

        this->valueChanged.invoke(newValue, invocationArgs);
        this->simpleValueChanged.invoke(invocationArgs);
    }

    void
    resetToDefaultValue(SignalArgs &&args)
    {
        // Preserve hasBeenSet state
        bool tmp = this->valueHasBeenSet;

        this->setValue(this->defaultValue, std::move(args));

        this->valueHasBeenSet = tmp;
    }

    void
    setDefaultValue(const Type &newDefaultValue)
    {
        this->defaultValue = newDefaultValue;
    }

    Type
    getDefaultValue() const
    {
        return this->defaultValue;
    }

    Type
    getValue() const
    {
        return this->value;
    }

    Signals::Signal<const Type &, const SignalArgs &> valueChanged;

private:
    Type defaultValue;
    Type value;

    Type *
    getValuePointer()
    {
        return &this->value;
    }

    friend class SettingManager;
};

}  // namespace Settings
}  // namespace pajlada
