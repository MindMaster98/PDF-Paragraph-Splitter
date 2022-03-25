#include <iostream>
#include <filesystem>
#include <fstream>
#include <stack>
#include <queue>
#include <string>
#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-toc.h>
#include <poppler/cpp/poppler-page.h>
#include <regex>
#include "include/nlohmann/json.hpp"

/***
 * Get Levenshtein distance of 2 strings
 * @param s1 first string
 * @param s2 second string
 * @return Levenshtein distance of both strings
 */
unsigned int distance(const std::string& s1, const std::string& s2)
{
    const std::size_t len1 = s1.size(), len2 = s2.size();
    std::vector<std::vector<unsigned int>> d(len1 + 1, std::vector<unsigned int>(len2 + 1));

    d[0][0] = 0;
    for(unsigned int i = 1; i <= len1; ++i) d[i][0] = i;
    for(unsigned int i = 1; i <= len2; ++i) d[0][i] = i;

    for(unsigned int i = 1; i <= len1; ++i) {
        for(unsigned int j = 1; j <= len2; ++j) {
            d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + (s1[i - 1] == s2[j - 1] ? 0 : 1)});
        }
    }
    return d[len1][len2];
}

/***
 * Extract the text of a PDF page into sections
 * @param sections list for all section titles
 * @param sectionTexts list of all sections
 * @param content PDF page content
 * @param usedSections list of already processed sections
 */
void extractText(std::stack<std::string>& sections, std::vector<std::string>& sectionTexts,
                 std::string content, std::queue<std::string>& usedSections) {
    // run until the full page has been processed
    do {
        std::string separator;

        // there are sections available for extraction
        if(!sections.empty()) {
            // get first section from stack
            separator = sections.top();
        }
        else {
            return;
        }

        // similarity threshold for section title detection
        float threshold = std::round((float)separator.length() * 0.1f);

        std::string first_segment;

        // Levenshtein distance of section title and page content and title position
        unsigned int dist = -1;
        int pos = 0;

        // iterate over page from bottom to top
        for(int i = (int)content.size() - (int)separator.size(); i >= (int)separator.size(); i--) {
            unsigned int dist_before = dist;

            // select substring with current section title's length
            std::string substring = content.substr(i - separator.size(), separator.size());

            // calculate Levenshtein distance
            dist = std::min(dist, distance(substring, separator));

            // distance decreased
            if(dist != dist_before) {
                // update position
                pos = i - (int) separator.size();
            }

            // stop, if exact match found
            if(dist == 0) {
                break;
            }
        }

        // shift start position of section to the left if section starts with special unicode characters
        while(pos > 0 && char(content[pos]) < 0) {
            pos--;
        }

        // section title not found
        if((float)dist > threshold) {
            // select full remaining content
            first_segment = content;
        }
        else {
            // select content after section title
            first_segment = content.substr(pos);
        }

        // append segment to the last found section
        sectionTexts.back().append(first_segment);

        // section title found
        if((float)dist <= threshold) {
            // select remaining content
            content = content.substr(0, pos);

            // create new section and move to next title
            sections.pop();
            sectionTexts.emplace_back("");

            // store title of finished section
            usedSections.push(separator);
        }
        else {
            break;
        }
    } while(true);
}

/***
 * Convert PDF unicode string to basic UTF-8 string
 * @param text PDF unicode string
 * @return converted basic string
 */
std::string toUTF8(const poppler::ustring& text) {
    poppler::byte_array titleArray = text.to_utf8();
    return std::string { titleArray.data(), titleArray.size() };
}

/***
 * Read the table of contents of a PDF file
 * @param tocStack list of all section titles
 * @param tocItem root node of ToC tree
 */
void loadTOC(std::stack<std::string>& tocStack, const poppler::toc_item& tocItem) {
    for(poppler::toc_item* section: tocItem.children()) {
        std::string label = toUTF8(section->title());

        // remove multiple white spaces
        std::regex space_re(R"(\s+)");
        label = std::regex_replace(label, space_re, " ");

        tocStack.push(label);
    }
}

/***
 * Convert a PDF file into JSON list of sections
 * @param file PDF file path
 * @param language PDF text language
 */
void convertPDF(const std::string& file, const std::string& language) {
    // get file name
    std::string fileName = file.substr(file.find_last_of('/') + 1);

    // open PDF and read title
    poppler::document* document = poppler::document::load_from_file(file);
    std::string title = toUTF8(document->get_title());

    // table of contents of the PDF
    poppler::toc* fileTOC = document->create_toc();

    std::stack<std::string> sections = std::stack<std::string>();

    // ToC available
    if(fileTOC != nullptr) {
        loadTOC(sections, *fileTOC->root());
    }
    else {
        // Log unsupported file
        std::cout << title << std::endl;
        return;
    }

    std::vector<std::string> sectionTexts{""};
    std::queue<std::string> usedSections{};

    // iterate over all pages from back to front
    for(int i = document->pages() - 1; i >= 0; i--) {
        // load page and read text
        poppler::page* page = document->create_page(i);
        std::string sectionText = toUTF8(page->text());

        // remove multiple whitespaces
        std::regex space_re(R"(\s+)");
        sectionText = std::regex_replace(sectionText, space_re, " ");

        // find sections in page text
        extractText(sections, sectionTexts, sectionText, usedSections);

        delete page;
    }

    delete document;
    delete fileTOC;

    // remove sections not related to section titles
    while(sectionTexts.size() > usedSections.size()) {
        sectionTexts.erase(sectionTexts.end());
    }

    nlohmann::json json;

    // create json object foreach section
    for(std::string section: sectionTexts) {
        nlohmann::json sectionJson{
                {"title", title},
                {"topic", fileName},
                {"language", language},
                {"text", section},
                {"paragraph", usedSections.front()}
        };

        json.push_back(sectionJson);
        usedSections.pop();
    }

    // write json format of section list to a file
    std::ofstream out("output.json", std::ofstream::in | std::ofstream::app);

    std::string output = json.dump();
    out << output << std::endl;

    out.close();
}

/***
 * Convert all PDF files of the given directory and subdirectories
 * @param dir root directory
 * @param language language of PDF texts
 */
void convertDirectory(const std::string& dir, const std::string& language) {
    for(auto& entry: std::filesystem::directory_iterator(dir)) {
        if(entry.is_directory()) {
            convertDirectory(entry.path(), language);
        }
        else {
            convertPDF(entry.path(), language);
        }
    }
}

/***
 * run PDF section to JSON conversion for all files in all given directories
 * @param argc list of arguments
 * @param argv language tag + list of directories with PDF files
 * @return status code
 */
int main(int argc, char **argv) {
    if(argc < 3) {
        std::cout << "Please enter a language tag and a path to a PDF file" << std::endl;
    }
    else {
        std::filesystem::remove("output.json");
        std::string language = argv[1];

        for(int i = 2; i < argc; i++) {
            std::string path = argv[i];

            if(std::filesystem::is_directory(path)) {
                convertDirectory(path, language);
            }
            else {
                convertPDF(path, language);
            }
        }
    }

    return 0;
}
