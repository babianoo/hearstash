#include "ui/settings_store.h"

#include <exception>
#include <format>
#include <iostream>

// FLTK
#include <FL/Fl.H>

// Seconds without a change before saving
static constexpr double SAVE_DELAY = 0.5;

SettingsStore::SettingsStore(const std::filesystem::path& file)
    : file(file), settings(load_settings(file)){}

SettingsStore::~SettingsStore(){
    Fl::remove_timeout(on_save_timeout, this);
    if (unsaved) save();
}

void SettingsStore::update(const std::function<void(Settings&)>& change){
    change(settings);
    sanitize(settings);
    unsaved = true;
    // Restarts the wait, so a run of changes is saved once, after the last
    Fl::remove_timeout(on_save_timeout, this);
    Fl::add_timeout(SAVE_DELAY, on_save_timeout, this);
}

void SettingsStore::on_save_timeout(void* data){
    static_cast<SettingsStore*>(data)->save();
}

void SettingsStore::save(){
    try {
        save_settings(file, settings);
        unsaved = false;
    } catch (const std::exception& e){
        // Not in a dialog, which would pop up in the middle of typing
        std::cerr << std::format("Couldn't save the settings: {}\n", e.what());
    }
}
