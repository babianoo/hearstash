#include "ui/jisho_format.h"

#include <format>
#include <string>
#include <string_view>

#include "ui/theme.h"

// Looks of the parts of a Jisho result
static const TextStyle WORD{FL_HELVETICA_BOLD, 24};
static const TextStyle READING{FL_HELVETICA, 16, DIM};
static const TextStyle COMMON{FL_HELVETICA_BOLD, 12, fl_rgb_color(40, 120, 40)};
static const TextStyle NUMBER{FL_HELVETICA, 14, DIM};
static const TextStyle DEFINITION{};
static const TextStyle PART_OF_SPEECH{FL_HELVETICA_ITALIC, 12, DIM};
static const TextStyle OTHER_FORMS{FL_HELVETICA, 13, DIM};

static std::string join(const std::vector<std::string>& parts, std::string_view separator){
    std::string joined;
    for (size_t i = 0; i < parts.size(); ++i){
        if (i > 0) joined += separator;
        joined += parts[i];
    }
    return joined;
}

// The results, styled a bit like jisho.org:
//
//   兄弟  きょうだい  common word          (big bold word, grey reading)
//       1. brothers and sisters; siblings  Noun
//       Other forms: 兄弟 (けいてい)
std::vector<TextSpan> format_entries(const std::vector<JishoEntry>& entries){
    std::vector<TextSpan> spans;
    for (const JishoEntry& entry : entries){
        if (!entry.forms.empty()){
            // Kana-only words have just the reading, which then is the word
            const JishoForm& main = entry.forms.front();
            if (main.word.empty()){
                spans.push_back({main.reading, WORD});
            } else {
                spans.push_back({main.word, WORD});
                spans.push_back({"  " + main.reading, READING});
            }
        }
        if (entry.is_common) spans.push_back({"  common word", COMMON});
        spans.push_back({"\n"});

        int n = 1;
        for (const JishoSense& sense : entry.senses){
            spans.push_back({std::format("    {}. ", n++), NUMBER});
            spans.push_back({join(sense.definitions, "; "), DEFINITION});
            if (!sense.parts_of_speech.empty())
                spans.push_back({"  " + join(sense.parts_of_speech, ", "), PART_OF_SPEECH});
            spans.push_back({"\n"});
        }

        if (entry.forms.size() > 1){
            std::vector<std::string> others;
            for (size_t i = 1; i < entry.forms.size(); ++i){
                const JishoForm& form = entry.forms[i];
                if (form.word.empty()) others.push_back(form.reading);
                else others.push_back(std::format("{} ({})", form.word, form.reading));
            }
            spans.push_back({"    Other forms: " + join(others, ", ") + '\n', OTHER_FORMS});
        }
        spans.push_back({"\n"});
    }
    return spans;
}
