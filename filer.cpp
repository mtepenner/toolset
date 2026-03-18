#include <iostream>
#include <filesystem>
#include <string>
#include <fstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <unordered_set>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

// --- CONFIGURATION ---
const fs::path WATCH_DIR = "./unprocessed_scans";
const fs::path ARCHIVE_DIR = "./Scanned_Archive";
const std::string AI_ENDPOINT = "http://localhost:11434/api/generate"; // Default Ollama endpoint
const std::string AI_MODEL = "mistral"; // Change to your local model

// Callback function to capture cURL response
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// 1. OCR Step: Extract text using Tesseract command line
std::string extractText(const fs::path& filePath) {
    std::string tempFile = "temp_ocr_out";
    // Tesseract automatically appends .txt to the output file name
    std::string cmd = "tesseract \"" + filePath.string() + "\" " + tempFile + " -l eng quiet";
    
    std::cout << "Running OCR on: " << filePath.filename() << "..." << std::endl;
    int result = system(cmd.c_str());
    
    if (result != 0) {
        std::cerr << "Tesseract failed. Is it installed and in your PATH?" << std::endl;
        return "";
    }

    std::ifstream inFile(tempFile + ".txt");
    std::string extractedText((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    inFile.close();
    
    // Clean up temp file
    fs::remove(tempFile + ".txt");
    
    return extractedText;
}

// 2. AI Inference Step: Ask local LLM to categorize and name
json getAiSuggestion(const std::string& extractedText) {
    CURL* curl = curl_easy_init();
    std::string response_string;
    json resultJson;

    if(curl) {
        // Limit text length to avoid token limits on smaller local models
        std::string truncatedText = extractedText.substr(0, std::min(extractedText.length(), (size_t)1000));
        
        std::string prompt = 
            "Analyze this OCR text from a scanned document. Return ONLY a valid JSON object with no markdown formatting. "
            "It must have two keys: 'category' (a single descriptive word or underscored phrase like 'Taxes' or 'Medical_Records') "
            "and 'filename' (a short, descriptive filename ending in .pdf based on the content). "
            "Text to analyze: \n\n" + truncatedText;

        json requestData = {
            {"model", AI_MODEL},
            {"prompt", prompt},
            {"stream", false},
            {"format", "json"} // Supported by Ollama to force JSON output
        };

        std::string data = requestData.dump();

        curl_easy_setopt(curl, CURLOPT_URL, AI_ENDPOINT.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

        CURLcode res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            std::cerr << "cURL failed: " << curl_easy_strerror(res) << std::endl;
        } else {
            try {
                // Parse the response from Ollama
                json fullResponse = json::parse(response_string);
                std::string aiResponseText = fullResponse["response"];
                // Parse the actual JSON the model generated
                resultJson = json::parse(aiResponseText);
            } catch (const std::exception& e) {
                std::cerr << "Failed to parse AI JSON response: " << e.what() << std::endl;
                std::cerr << "Raw AI Output: " << response_string << std::endl;
            }
        }
        curl_easy_cleanup(curl);
    }
    return resultJson; 
}

// 3. File Management Step: Move and rename the file
void organizeFile(const fs::path& sourceFile, const std::string& category, std::string newFileName) {
    std::string safeCategory = category;
    std::replace(safeCategory.begin(), safeCategory.end(), ' ', '_');
    
    fs::path targetDir = ARCHIVE_DIR / safeCategory;

    try {
        if (!fs::exists(targetDir)) {
            std::cout << "Creating new category directory: " << targetDir << std::endl;
            fs::create_directories(targetDir);
        }

        fs::path destination = targetDir / newFileName;

        // Prevent overwriting existing files
        int counter = 1;
        while (fs::exists(destination)) {
            fs::path stem = fs::path(newFileName).stem();
            fs::path ext = fs::path(newFileName).extension();
            destination = targetDir / (stem.string() + "_" + std::to_string(counter++) + ext.string());
        }

        fs::rename(sourceFile, destination);
        std::cout << "Successfully moved to: " << destination << "\n" << std::endl;

    } catch (const fs::filesystem_error& e) {
        std::cerr << "File System Error: " << e.what() << std::endl;
    }
}

// Watcher Step: Monitor the directory for new files
void watchDirectory() {
    std::unordered_set<std::string> processedFiles;

    std::cout << "Watching directory: " << WATCH_DIR << "..." << std::endl;
    
    while (true) {
        for (const auto& entry : fs::directory_iterator(WATCH_DIR)) {
            if (entry.is_regular_file()) {
                std::string filePath = entry.path().string();
                
                // If we haven't processed this file yet
                if (processedFiles.find(filePath) == processedFiles.end()) {
                    std::cout << "\nNew file detected: " << entry.path().filename() << std::endl;
                    
                    std::string extractedText = extractText(entry.path());
                    
                    if (!extractedText.empty()) {
                        json aiData = getAiSuggestion(extractedText);
                        
                        if (aiData.contains("category") && aiData.contains("filename")) {
                            std::string category = aiData["category"];
                            std::string newFileName = aiData["filename"];
                            
                            // Ensure the new filename retains the original extension
                            std::string ext = entry.path().extension().string();
                            if (fs::path(newFileName).extension() != ext) {
                                newFileName = fs::path(newFileName).stem().string() + ext;
                            }

                            organizeFile(entry.path(), category, newFileName);
                        } else {
                            std::cerr << "AI did not return the expected 'category' and 'filename' fields." << std::endl;
                        }
                    }
                    processedFiles.insert(filePath); // Mark as processed (or attempted)
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(5)); // Check every 5 seconds
    }
}

int main() {
    // Ensure base directories exist
    if (!fs::exists(WATCH_DIR)) fs::create_directories(WATCH_DIR);
    if (!fs::exists(ARCHIVE_DIR)) fs::create_directories(ARCHIVE_DIR);

    // Initialize cURL globally
    curl_global_init(CURL_GLOBAL_DEFAULT);

    watchDirectory();

    curl_global_cleanup();
    return 0;
}
