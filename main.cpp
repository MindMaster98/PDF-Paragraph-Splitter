#include <iostream>
#include <filesystem>
#include <fstream>
#include <queue>
#include <string>
#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-toc.h>
#include <poppler/cpp/poppler-page.h>
#include <regex>
#include "include/nlohmann/json.hpp"

void extractText(std::queue<std::string>& sections, std::vector<std::string>& sectionTexts,
                 std::string content, int page, std::queue<std::string>& usedSections) {
    if(page == 0) {
        auto tocContent = content.find(sections.front());

        if(tocContent != std::string::npos) {
            content = content.replace(tocContent, sections.front().size(), " ");
        }
    }

    do {
        std::string separator{"  "};

        if(!sections.empty()) {
            separator = sections.front();
        }

        auto sep = content.find(separator);

        std::string first_segment = content.substr(0, sep);

        sectionTexts.back() += first_segment;

        if(sep != std::string::npos) {
            content = content.substr(sep + separator.size());
            sections.pop();
            sectionTexts.emplace_back("");
            usedSections.push(separator);
        }
        else {
            break;
        }
    } while(true);
}

std::string toUTF8(const poppler::ustring& text) {
    poppler::byte_array titleArray = text.to_utf8();
    return std::string { titleArray.data(), titleArray.size() };
}

void loadTOC(std::queue<std::string>& tocQueue, const poppler::toc_item& tocItem) {
    for(poppler::toc_item* section: tocItem.children()) {
        std::string label = toUTF8(section->title());

        std::regex space_re(R"(\s+)");
        label = std::regex_replace(label, space_re, " ");

        tocQueue.push(label);
    }
}

void convertPDF(const std::string& file) {
    std::string fileName = file.substr(file.find_last_of('/') + 1);
    poppler::document* document = poppler::document::load_from_file(file);
    std::string title = toUTF8(document->get_title());

    poppler::toc* fileTOC = document->create_toc();

    std::queue<std::string> sections = std::queue<std::string>();

    bool headers = false;

    if(fileTOC != nullptr) {
        loadTOC(sections, *fileTOC->root());

        while(!sections.empty() && sections.front() != "Inhalt") {
            sections.pop();
        }

        if(!sections.empty()) {
            sections.pop();
        }

        headers = !sections.empty();
    }
    else {
        std::cout << title << std::endl;
    }

    if(!headers) {
        return;
    }

    std::vector<std::string> sectionTexts{""};
    std::queue<std::string> usedSections{};
    usedSections.push("Intro");

    for(int i = 0; i < document->pages(); i++) {
        poppler::page* page = document->create_page(i);

        std::string sectionText = toUTF8(page->text());

        std::regex space_re(R"(\s+)");
        sectionText = std::regex_replace(sectionText, space_re, " ");

        extractText(sections, sectionTexts, sectionText, i, usedSections);

        delete page;
    }

    delete document;
    delete fileTOC;

    nlohmann::json json;

    for(const std::string& section: sectionTexts) {
        nlohmann::json sectionJson{
                {"title", title},
                {"topic", fileName},
                {"language", "de"},
                {"text", section},
                {"paragraph", usedSections.front()}
        };

        json.push_back(sectionJson);
        usedSections.pop();
    }

    if(json.size() == 1) {
        std::cout << json.size() << " " << title << std::endl;
    }

    std::ofstream out("output.json", std::ofstream::in | std::ofstream::app);
    out << json.dump() << std::endl;
    out.close();
}

void convertDirectory(const std::string& dir) {
    for(auto& entry: std::filesystem::directory_iterator(dir)) {
        if(entry.is_directory()) {
            convertDirectory(entry.path());
        }
        else {
            convertPDF(entry.path());
        }
    }
}

int main(int argc, char **argv) {
    if(argc < 2) {
        std::cout << "Please enter a path to a PDF file" << std::endl;
    }
    else {
        std::filesystem::remove("output.json");

        for(int i = 1; i < argc; i++) {
            std::string path = argv[i];

            if(std::filesystem::is_directory(path)) {
                convertDirectory(path);
            }
            else {
                convertPDF(path);
            }
        }
    }

    return 0;
}
