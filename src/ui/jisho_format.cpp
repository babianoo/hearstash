#include "ui/jisho_format.h"

#include <format>
#include <string>
#include <string_view>

#include "ui/theme.h"

// Looks of the parts of a result
static const TextStyle WORD{FL_HELVETICA_BOLD, 24};
static const TextStyle READING{FL_HELVETICA, 16, DIM};
static const TextStyle COMMON{FL_HELVETICA_BOLD, 12, fl_rgb_color(40, 120, 40)};
static const TextStyle NUMBER{FL_HELVETICA, 14, DIM};
static const TextStyle DEFINITION{};
static const TextStyle PART_OF_SPEECH{FL_HELVETICA_ITALIC, 12, DIM};
static const TextStyle OTHER_FORMS{FL_HELVETICA, 13, DIM};
static const TextStyle FREQUENCY_SOURCE{FL_HELVETICA_BOLD, 12, fl_rgb_color(30, 110, 130)};
static const TextStyle FREQUENCY{FL_HELVETICA, 12, DIM};

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
//   兄弟  きょうだい  common word  JPDBv2㋕ <rank>   (big bold word, grey reading)
//       1. brothers and sisters; siblings  Noun
//       Other forms: 兄弟 (けいてい)
std::vector<TextSpan> format_entries(const std::vector<WordEntry>& entries){
    std::vector<TextSpan> spans;
    for (const WordEntry& entry : entries){
        if (!entry.forms.empty()){
            // Kana-only words have just the reading, which then is the word
            const WordForm& main = entry.forms.front();
            if (main.word.empty()){
                spans.push_back({main.reading, WORD});
            } else {
                spans.push_back({main.word, WORD});
                spans.push_back({"  " + main.reading, READING});
            }
        }
        if (entry.is_common) spans.push_back({"  common word", COMMON});
        // As Yomitan shows it: JPDBv2㋕ 312, 19896㋕
        if (!entry.frequencies.empty()){
            spans.push_back({"  " + entry.frequency_source + " ", FREQUENCY_SOURCE});
            spans.push_back({join(entry.frequencies, ", "), FREQUENCY});
        }
        spans.push_back({"\n"});

        int n = 1;
        for (const WordSense& sense : entry.senses){
            spans.push_back({std::format("    {}. ", n++), NUMBER});
            spans.push_back({join(sense.definitions, "; "), DEFINITION});
            if (!sense.parts_of_speech.empty())
                spans.push_back({"  " + join(sense.parts_of_speech, ", "), PART_OF_SPEECH});
            spans.push_back({"\n"});
        }

        if (entry.forms.size() > 1){
            std::vector<std::string> others;
            for (size_t i = 1; i < entry.forms.size(); ++i){
                const WordForm& form = entry.forms[i];
                if (form.word.empty()) others.push_back(form.reading);
                else others.push_back(std::format("{} ({})", form.word, form.reading));
            }
            spans.push_back({"    Other forms: " + join(others, ", ") + '\n', OTHER_FORMS});
        }
        spans.push_back({"\n"});
    }

    // Credit where the definitions are from: the dictionary's licence
    // asks for it, and it tells a result from the file on this computer
    // from one fetched from jisho.org
    std::string source;
    for (const WordEntry& entry : entries){
        if (entry.source.empty() || source.find(entry.source) != std::string::npos) continue;
        if (!source.empty()) source += ", ";
        source += entry.source;
    }
    if (!source.empty()) spans.push_back({"From " + source + '\n', OTHER_FORMS});
    return spans;
}
