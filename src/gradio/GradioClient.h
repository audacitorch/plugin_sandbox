/**
 * @file
 * @brief A GradioClient class for making http requests to the Gradio API
 * @author  xribene
 */

#pragma once

#include <fstream>
#include "juce_core/juce_core.h"
#include "utils.h"

using CtrlList = std::vector<std::pair<juce::Uuid, std::shared_ptr<Ctrl>>>;

class GradioClient

{
public:
    // GradioClient(const juce::String& spaceUrl);
    GradioClient() = default;

    // Example method to get controls from the Gradio API
    void getControls(juce::Array<juce::var>& ctrlList, juce::DynamicObject& cardDict, juce::String& error);
    void setSpaceInfo(const juce::String url);
    SpaceInfo getSpaceInfo() const;
    
    // A static counter 
    
private:
    static void parseSpaceAddress(juce::String spaceAddress, SpaceInfo& spaceInfo);
    /***
    We parse the space address given by the user
    which can take 4 forms: 
        "http://localhost:7860", (gradio app)
        "https://xribene-midi-pitch-shifter.hf.space/", (gradio app)
        "https://huggingface.co/spaces/xribene/midi_pitch_shifter", (hf repo)
        "xribene/midi_pitch_shifter",
    
    and we store the parsed information in a SpaceInfo object
    e.g
    {
        "huggingface": "https://huggingface.co/spaces/xribene/midi_pitch_shifter",
        "gradio": "https://xribene-midi-pitch-shifter.hf.space/",
        "userInput": "xribene/midi_pitch_shifter",
        "modelName": "midi_pitch_shifter",
        "userName": "xribene"
    }
    ***/    
    SpaceInfo spaceInfo;
    
};