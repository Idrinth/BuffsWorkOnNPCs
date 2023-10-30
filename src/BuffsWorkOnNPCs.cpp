#include <latch>
#include "logger.h"

class Config {
public:
    spdlog::level::level_enum GetLogLevel() {
        return logLevel;
    }
    static const Config& GetSingleton() noexcept {
        static Config instance;

        static std::atomic_bool initialized;
        if (!initialized.exchange(true)) {
            instance.logLevel = spdlog::level::level_enum::info;
            std::ifstream inputFile(R"(Data\SKSE\Plugins\IdrinthBuffsWorkOnNPCs.yaml)");
            if (inputFile.good()) {
                for (std::string line; std::getline(inputFile, line); )
                {
                    if (line.starts_with("logLevel")) {
                        if (line.ends_with("trace")) {
                            instance.logLevel = spdlog::level::level_enum::trace;
                        } else if (line.ends_with("debug")) {
                            instance.logLevel = spdlog::level::level_enum::debug;
                        } else if (line.ends_with("info")) {
                            instance.logLevel = spdlog::level::level_enum::info;
                        } else if (line.ends_with("warn")) {
                            instance.logLevel = spdlog::level::level_enum::warn;
                        } else if (line.ends_with("error")) {
                            instance.logLevel = spdlog::level::level_enum::err;
                        } else if (line.ends_with("critical")) {
                            instance.logLevel = spdlog::level::level_enum::critical;
                        }
                    }
                }
                inputFile.close();
            }
        }

        return instance;
    }
private:
    spdlog::level::level_enum logLevel;
};

class ActorValueContainer {
private:
    float base;
    int level;
    long npc;
    RE::ActorValue value;
    RE::ActorValue modifier;
public:
    ActorValueContainer(RE::ActorValue value, RE::ActorValue modifier)
    {
        base = -1;
        level = -1;
        this->value = value;
        this->modifier = modifier;
        npc = -1;
    }
    void SetTotal(RE::Actor* actor)
    {
        int levelValue = actor->GetLevel();
        if (level != levelValue || npc != actor->GetActorBase()->GetFormID()) {
            level = levelValue;
            npc = actor->GetActorBase()->GetFormID();
            base = actor->GetActorBase()->GetActorValue(value);
        }
        logger::debug(
                "{}({}): {}+{}={}",
                actor->GetName(),
                value,
                base,
                actor->AsActorValueOwner()->GetActorValue(modifier),
                base + actor->AsActorValueOwner()->GetActorValue(modifier)
                );
        actor->AsActorValueOwner()->SetActorValue(value, base + actor->AsActorValueOwner()->GetActorValue(modifier));
    }
};

class BuffingEventSink: public RE::BSTEventSink<RE::TESActiveEffectApplyRemoveEvent>, public RE::BSTEventSink<RE::TESEquipEvent>, public RE::BSTEventSink<SKSE::CrosshairRefEvent> {
    BuffingEventSink() = default;
    BuffingEventSink(const BuffingEventSink&) = delete;
    BuffingEventSink(BuffingEventSink&&) = delete;
    BuffingEventSink& operator=(const BuffingEventSink&) = delete;
    BuffingEventSink& operator=(BuffingEventSink&&) = delete;

public:
    static BuffingEventSink* GetSingleton() {
        static BuffingEventSink singleton;
        if (singleton.perk) {
            singleton.perk = RE::BGSPerk::LookupByID<RE::BGSPerk>(0xCF788);//Skill Boost Perk
        }
        return &singleton;
    }
    RE::BSEventNotifyControl ProcessEvent(const SKSE::CrosshairRefEvent* event, RE::BSTEventSource<SKSE::CrosshairRefEvent>* eventSource)
    {
        logger::debug("Crosshair Ref changed");
        if (!event->crosshairRef) {
            logger::trace("-no target");
            return RE::BSEventNotifyControl::kContinue;
        }
        if (event->crosshairRef->IsPlayer() || event->crosshairRef->IsPlayerRef()) {
            logger::trace("-Player");
            return RE::BSEventNotifyControl::kContinue;
        }
        if (event->crosshairRef->IsDead()) {
            logger::trace("-dead");
            return RE::BSEventNotifyControl::kContinue;
        }
        if (event->crosshairRef->GetFormType() != RE::Actor::FORMTYPE) {
            logger::trace("-not actor");
            return RE::BSEventNotifyControl::kContinue;
        }
        auto* actor = RE::Actor::LookupByID(event->crosshairRef->formID)->As<RE::Actor>();
        ProcessActor(actor);
        return RE::BSEventNotifyControl::kContinue;
    }
    RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* event, RE::BSTEventSource<RE::TESEquipEvent>* eventSource)
    {
        logger::debug("Equipment changed");
        if (event->actor->IsPlayer() || event->actor->IsPlayerRef()) {
            logger::trace("-PLAYER");
            return RE::BSEventNotifyControl::kContinue;
        }
        if (event->actor->IsDead()) {
            logger::trace("-deaD");
            return RE::BSEventNotifyControl::kContinue;
        }
        auto* actor = RE::Actor::LookupByID(event->actor->formID)->As<RE::Actor>();
        ProcessActor(actor);
        return RE::BSEventNotifyControl::kContinue;
    }
    RE::BSEventNotifyControl ProcessEvent(const RE::TESActiveEffectApplyRemoveEvent* event, RE::BSTEventSource<RE::TESActiveEffectApplyRemoveEvent>* eventSource)
    {
        logger::trace("Effects changed");
        if (event->target->IsPlayer() || event->target->IsPlayerRef()) {
            logger::trace("-PLAYER");
            return RE::BSEventNotifyControl::kContinue;
        }
        if (event->target->IsDead()) {
            logger::trace("-dead");
            return RE::BSEventNotifyControl::kContinue;
        }
        if (event->target->GetFormType() != RE::Actor::FORMTYPE) {
            logger::trace("-not actor");
            return RE::BSEventNotifyControl::kContinue;
        }
        auto* actor = RE::Actor::LookupByID(event->target->formID)->As<RE::Actor>();
        ProcessActor(actor);
        return RE::BSEventNotifyControl::kContinue;
    }
    void reset()
    {
        logger::info("Clearing data");
        baseActors.clear();
    }
private:
    std::map<RE::FormID, std::map<RE::ActorValue, ActorValueContainer>> baseActors;
    RE::BGSPerk* perk;
    void ProcessActor(RE::Actor* actor)
    {
        if (actor->HasPerk(perk)) {
            logger::trace("Perk already working on {}", actor->GetName());
            return;
        }
        logger::trace("Processing {}", actor->GetName());
        if (!baseActors.contains(actor->GetFormID())) {
            baseActors.insert_or_assign(actor->GetFormID(), *new std::map<RE::ActorValue, ActorValueContainer>());
            baseActors.at(actor->GetFormID()).insert_or_assign(RE::ActorValue::kAlteration, *new ActorValueContainer(RE::ActorValue::kAlteration, RE::ActorValue::kAlterationModifier));
            baseActors.at(actor->GetFormID()).insert_or_assign(RE::ActorValue::kArchery, *new ActorValueContainer(RE::ActorValue::kArchery, RE::ActorValue::kMarksmanModifier));
            baseActors.at(actor->GetFormID()).insert_or_assign(RE::ActorValue::kBlock, *new ActorValueContainer(RE::ActorValue::kBlock, RE::ActorValue::kBlockModifier));
            baseActors.at(actor->GetFormID()).insert_or_assign(RE::ActorValue::kConjuration, *new ActorValueContainer(RE::ActorValue::kConjuration, RE::ActorValue::kConjurationModifier));
            baseActors.at(actor->GetFormID()).insert_or_assign(RE::ActorValue::kDestruction, *new ActorValueContainer(RE::ActorValue::kDestruction, RE::ActorValue::kDestructionModifier));
            baseActors.at(actor->GetFormID()).insert_or_assign(RE::ActorValue::kHeavyArmor, *new ActorValueContainer(RE::ActorValue::kHeavyArmor, RE::ActorValue::kHeavyArmorModifier));
            baseActors.at(actor->GetFormID()).insert_or_assign(RE::ActorValue::kIllusion, *new ActorValueContainer(RE::ActorValue::kIllusion, RE::ActorValue::kIllusionModifier));
            baseActors.at(actor->GetFormID()).insert_or_assign(RE::ActorValue::kLightArmor, *new ActorValueContainer(RE::ActorValue::kLightArmor, RE::ActorValue::kLightArmorModifier));
            baseActors.at(actor->GetFormID()).insert_or_assign(RE::ActorValue::kOneHanded, *new ActorValueContainer(RE::ActorValue::kOneHanded, RE::ActorValue::kOneHandedModifier));
            baseActors.at(actor->GetFormID()).insert_or_assign(RE::ActorValue::kRestoration, *new ActorValueContainer(RE::ActorValue::kRestoration, RE::ActorValue::kRestorationModifier));
            baseActors.at(actor->GetFormID()).insert_or_assign(RE::ActorValue::kSneak, *new ActorValueContainer(RE::ActorValue::kSneak, RE::ActorValue::kSneakingModifier));
            baseActors.at(actor->GetFormID()).insert_or_assign(RE::ActorValue::kTwoHanded, *new ActorValueContainer(RE::ActorValue::kTwoHanded, RE::ActorValue::kTwoHandedModifier));
        }
        for (auto pair : baseActors.at(actor->GetFormID())) {
            pair.second.SetTotal(actor);
        }
        logger::trace("Processed {}", actor->GetName());
    }
};
void OnMessage(SKSE::MessagingInterface::Message* message) {
    switch(message->type) {
        case SKSE::MessagingInterface::kNewGame:
        case SKSE::MessagingInterface::kPreLoadGame:
            BuffingEventSink::GetSingleton()->reset();
    }
}
SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    auto config = Config::GetSingleton();
    SetupLogger(config.GetLogLevel());
    logger::info("Logger set up at level {}", config.GetLogLevel());

    auto* eventSink = BuffingEventSink::GetSingleton();

    auto* eventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
    eventSourceHolder->AddEventSink<RE::TESActiveEffectApplyRemoveEvent>(eventSink);
    eventSourceHolder->AddEventSink<RE::TESEquipEvent>(eventSink);

    SKSE::GetCrosshairRefEventSource()->AddEventSink(eventSink);

    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);

    return true;
}