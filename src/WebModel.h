/**
 * @file
 * @brief This file is part of the JUCE examples.
 * @brief Base class for any models that utilize a web api to process
 * information. Currently we provide an implmentation of a wave 2 wave web based
 * model. We use the gradio python client to communicate with a gradio server.
 * @author hugo flores garcia, aldo aguilar
 */

#pragma once

#include <fstream>


#include "Model.h"

#include "juce_core/juce_core.h"
// #include "juce_data_structres/juce_data_structures.h"


struct Ctrl {
  juce::Uuid id {""};
  std::string label {""};
  virtual ~Ctrl() = default; // virtual destructor
};

struct SliderCtrl : public Ctrl {
  double minimum;
  double maximum;
  double step;
  double value;
};

struct TextBoxCtrl : public Ctrl {
  std::string value;
};

struct AudioInCtrl : public Ctrl {
  std::string value;
};

struct MidiInCtrl : public Ctrl {
  std::string value;
};

struct NumberBoxCtrl : public Ctrl {
  double min;
  double max;
  double value;
};

struct ToggleCtrl : public Ctrl {
  bool value;
};

struct ComboBoxCtrl : public Ctrl {
  std::vector<std::string> options;
  std::string value;
};



using CtrlList = std::vector<std::pair<juce::Uuid, std::shared_ptr<Ctrl>>>;

namespace{

  void LogAndDBG(const juce::String& message) {
    DBG(message);

    juce::File logFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("HARP.log");
    logFile.appendText(message + "\n");
  }
}

class WebWave2Wave : public Model {
public:
  WebWave2Wave() { // TODO: should be a singleton
    juce::File logFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("HARP.log");
    logFile.deleteFile();
    m_status_flag_file.replaceWithText("Status.INITIALIZED");

    #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
      scriptPath = juce::File::getSpecialLocation(
        juce::File::currentApplicationFile
      ).getParentDirectory().getChildFile("Resources/gradiojuce_client/gradiojuce_client.exe");

      prefix_cmd = "start /B cmd /c set PYTHONIOENCODING=UTF-8 && ";
    #elif __APPLE__
      scriptPath = juce::File::getSpecialLocation(
          juce::File::currentApplicationFile
      ).getChildFile("Contents/Resources/gradiojuce_client/gradiojuce_client");
      prefix_cmd = "";
    #elif __linux__
      scriptPath = juce::File::getSpecialLocation(
          juce::File::currentApplicationFile
      ).getParentDirectory().getChildFile("Resources/gradiojuce_client/gradiojuce_client");
      prefix_cmd = "";
    #else
      #error "gradiojuce_client has not been implemented for this platform"
    #endif
  }

  ~WebWave2Wave() {
    // clean up flag files
    m_cancel_flag_file.deleteFile();
    m_status_flag_file.deleteFile();
  }


  bool ready() const override { return m_loaded; }
  std::string space_url() const { return m_url; }

  void load(const map<string, any> &params) override {
    m_ctrls.clear();
    m_loaded = false;

    // get the name of the huggingface repo we're going to use
    if (!modelparams::contains(params, "url")) {
        throw std::runtime_error("url not found in params");
    }

    std::string url = std::any_cast<std::string>(params.at("url"));
    m_url = url; // Store the URL for future use
    LogAndDBG("url: " + m_url);

    juce::File outputPath = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("control_spec.json");
    outputPath.deleteFile();

    juce::File tempLogFile =
    juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("system_get_ctrls_log.txt");
    tempLogFile.deleteFile();  // ensure the file doesn't already exist

    std::string command = (
      prefix_cmd
      + scriptPath.getFullPathName().toStdString()
      + " --mode get_ctrls"
      + " --url " + m_url
      + " --output_path " + outputPath.getFullPathName().toStdString()
      + " >> " + tempLogFile.getFullPathName().toStdString()   // redirect stdout to the temp log file
      + " 2>&1"   // redirect stderr to the same file as stdout
    );

    LogAndDBG("Running command: " + command);
    // TODO: urgently need to find a better alternative to system
    int result = std::system(command.c_str());

    juce::String logContent = tempLogFile.loadFileAsString();
    LogAndDBG(logContent);
    tempLogFile.deleteFile();  // delete the temporary log file

    if (result != 0) {
        // read the text from the temp log file.
        std::string message = "An error occurred while calling the gradiojuce helper with mode get_ctrls. Check the logs (~/Documents/HARP.log) for more details.\nLog content: " + logContent.toStdString();
        throw std::runtime_error(message);
    }


    // Load the output JSON and parse controls if needed (This step might need more detail based on your requirements)
    juce::var controls = loadJsonFromFile(outputPath);
    if (controls.isVoid()) {
        throw std::runtime_error("Failed to load controls from JSON. juce::var was void.");
    }

    juce::DynamicObject *ctrlDict = controls.getDynamicObject();
    if (ctrlDict == nullptr) {
        throw std::runtime_error("Failed to load control dict from JSON. ctrlDict is null.");
    }

    // the "ctrls" key should be a list of dicts
    // the "card" key should be the modelcard

    // BEGIN  MODELCARD
    if (!ctrlDict->hasProperty("card")) {
        throw std::runtime_error("Failed to load model card from JSON. card key not found.");
    }
    juce::DynamicObject *jsonCard = ctrlDict->getProperty("card").getDynamicObject();
    if (jsonCard == nullptr) {
        throw std::runtime_error("Failed to load model card from JSON.");
    }

    // TODO: probably need to check if these properties exist and if they're the right types.
    m_card = ModelCard();
    m_card.name = jsonCard->getProperty("name").toString().toStdString();
    m_card.description = jsonCard->getProperty("description").toString().toStdString();
    m_card.author = jsonCard->getProperty("author").toString().toStdString();
    m_card.midi_in = jsonCard->getProperty("midi_in").toString().toStdString();
    m_card.midi_out = jsonCard->getProperty("midi_out").toString().toStdString();
    // m_card.audio_in = jsonCard->getProperty("audio_in").toString().toStdString();
    // m_card.audio_out = jsonCard->getProperty("audio_out").toString().toStdString();

    // tags is a list of str
    juce::Array<juce::var> *tags = jsonCard->getProperty("tags").getArray();
    if (tags == nullptr) {
        throw std::runtime_error("Failed to load tags from JSON. tags is null.");
    }
    for (int i = 0; i < tags->size(); i++) {
      m_card.tags.push_back(tags->getReference(i).toString().toStdString());
    }
    // END MODELCARD

    if (!ctrlDict->hasProperty("ctrls")) {
        throw std::runtime_error("Failed to load controls from JSON. ctrls key not found.");
    }
    // else, it should be a list of dicts
    juce::Array<juce::var> *ctrlList = ctrlDict->getProperty("ctrls").getArray();
    if (ctrlList == nullptr) {
        throw std::runtime_error("Failed to load controls from JSON. ctrlList is null.");
    }

    // clear the m_ctrls vector
    m_ctrls.clear();

    // iterate through the list of controls
    // and add them to the m_ctrls vector
    for (int i = 0; i < ctrlList->size(); i++) {
      juce::var ctrl = ctrlList->getReference(i);
      if (!ctrl.isObject()) {
          throw std::runtime_error("Failed to load controls from JSON. ctrl is not an object.");
      }

      try{
          // get the ctrl type
          juce::String ctrl_type = ctrl["ctrl_type"].toString().toStdString();

          // For the first two, we are abusing the term control.
          // They are actually the main inputs to the model (audio or midi) 
          if (ctrl_type == "audio_in") {
            auto audio_in = std::make_shared<AudioInCtrl>();
            audio_in->label = ctrl["label"].toString().toStdString();

            m_ctrls.push_back({audio_in->id, audio_in});
            LogAndDBG("Audio In: " + audio_in->label + " added");
          }
          else if (ctrl_type == "midi_in") {
            auto midi_in = std::make_shared<MidiInCtrl>();
            midi_in->label = ctrl["label"].toString().toStdString();

            m_ctrls.push_back({midi_in->id, midi_in});
            LogAndDBG("MIDI In: " + midi_in->label + " added");
          }
          // The rest are the actual controls that map to hyperparameters
          // of the model
          else if (ctrl_type == "slider") {
            auto slider = std::make_shared<SliderCtrl>();
            slider->id = juce::Uuid();
            slider->label = ctrl["label"].toString().toStdString();
            slider->minimum = ctrl["minimum"].toString().getFloatValue();
            slider->maximum = ctrl["maximum"].toString().getFloatValue();
            slider->step = ctrl["step"].toString().getFloatValue();
            slider->value = ctrl["value"].toString().getFloatValue();

            m_ctrls.push_back({slider->id, slider});
            LogAndDBG("Slider: " + slider->label + " added");
          }
          else if (ctrl_type == "text") {
            auto text = std::make_shared<TextBoxCtrl>();
            text->id = juce::Uuid();
            text->label = ctrl["label"].toString().toStdString();
            text->value = ctrl["value"].toString().toStdString();

            m_ctrls.push_back({text->id, text});
            LogAndDBG("Text: " + text->label + " added");
          }
          else if (ctrl_type == "number_box") {
            auto number_box = std::make_shared<NumberBoxCtrl>();
            number_box->label = ctrl["label"].toString().toStdString();
            number_box->min = ctrl["min"].toString().getFloatValue();
            number_box->max = ctrl["max"].toString().getFloatValue();
            number_box->value = ctrl["value"].toString().getFloatValue();

            m_ctrls.push_back({number_box->id, number_box});
            LogAndDBG("Number Box: " + number_box->label + " added");
          }
          else {
            LogAndDBG("failed to parse control with unknown type: " + ctrl_type);
          }
        }
        catch (const char* e) {
          throw std::runtime_error("Failed to load controls from JSON. " + *e);
        }
      }

    outputPath.deleteFile();
    m_loaded = true;

    // set the status to LOADED
    m_status_flag_file.replaceWithText("Status.LOADED");
  }

  CtrlList& controls() {
    return m_ctrls;
  }

  void process(juce::File filetoProcess) const {
    // clear the cancel flag file
    m_cancel_flag_file.deleteFile();

    // make sure we're loaded
    LogAndDBG("WebWave2Wave::process");
    if (!m_loaded) {
      throw std::runtime_error("Model not loaded");
    }

    // a random string to append to the input/output.mid files
    // This is necessary because more than 1 playback regions
    // are processed at the same time.
    std::string randomString = juce::Uuid().toString().toStdString();

    // save the buffer to file
    LogAndDBG("Saving buffer to file");
    juce::File tempFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("input_" + randomString + ".mid");
    tempFile.deleteFile();
    // copy the file to a temp file
    filetoProcess.copyFileTo(tempFile);

    // a tarrget output file
    juce::File tempOutputFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("output_" + randomString + ".mid");
    tempOutputFile.deleteFile();

    // a ctrls file
    juce::File tempCtrlsFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("ctrls_" + randomString + ".json");
    tempCtrlsFile.deleteFile();

    LogAndDBG("saving controls...");
    if (!saveCtrls(tempCtrlsFile, tempFile.getFullPathName().toStdString())) {
      throw std::runtime_error("Failed to save controls to file.");
    }

    juce::File tempLogFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("system_log" + randomString + ".txt");
    tempLogFile.deleteFile();  // ensure the file doesn't already exist

    std::string command = (
        prefix_cmd
        + scriptPath.getFullPathName().toStdString()
        + " --mode predict"
        + " --url " + m_url
        + " --output_path " + tempOutputFile.getFullPathName().toStdString()
        + " --ctrls_path " + tempCtrlsFile.getFullPathName().toStdString()
        + " --cancel_flag_path " + m_cancel_flag_file.getFullPathName().toStdString()
        + " --status_flag_path " + m_status_flag_file.getFullPathName().toStdString()
        + " >> " + tempLogFile.getFullPathName().toStdString()   // redirect stdout to the temp log file
        + " 2>&1"   // redirect stderr to the same file as stdout
    );
    LogAndDBG("Running command: " + command);
    // TODO: log commmand output to a file
    int result = std::system(command.c_str());

    juce::String logContent = tempLogFile.loadFileAsString();
    LogAndDBG(logContent);

    if (result != 0) {
        // read the text from the temp log file.
        std::string message = "An error occurred while calling the gradiojuce helper with mode predict. Check the logs (~/Documents/HARP.log) for more details.\nLog content: " + logContent.toStdString();
        throw std::runtime_error(message);
    }

    tempLogFile.deleteFile();  // delete the temporary log file

    // move the temp output file to the original input file
    tempOutputFile.moveFileTo(filetoProcess);

    // delete the temp input file
    tempFile.deleteFile();
    tempOutputFile.deleteFile();
    tempCtrlsFile.deleteFile();
    LogAndDBG("WebWave2Wave::process done");

    // clear the cancel flag file
    m_cancel_flag_file.deleteFile();
    return;
  }

  // sets a cancel flag file that the client can check to see if the process
  // should be cancelled
  void cancel() {
    m_cancel_flag_file.deleteFile();
    m_cancel_flag_file.create();
  }

  std::string getStatus() {
    // if the status file doesn't exist, return Status.INACTIVE
    if (!m_status_flag_file.exists()) {
      return "Status.INACTIVE";
    }

    // read the status file and return its text
    juce::String status = m_status_flag_file.loadFileAsString();
    return status.toStdString();
  }

  juce::File getCancelFlagFile() const {
    return m_cancel_flag_file;
  }

  CtrlList::iterator findCtrlByUuid(const juce::Uuid& uuid) {
    return std::find_if(m_ctrls.begin(), m_ctrls.end(),
        [&uuid](const CtrlList::value_type& pair) {
            return pair.first == uuid;
        }
    );
  }

private:
  juce::var loadJsonFromFile(const juce::File& file) const {
    juce::var result;

    LogAndDBG("Loading JSON from file: " + file.getFullPathName());
    if (!file.existsAsFile()) {
        LogAndDBG("File does not exist: " + file.getFullPathName());
        return result;
    }

    juce::String fileContent = file.loadFileAsString();

    juce::Result parseResult = juce::JSON::parse(fileContent, result);

    if (parseResult.failed()) {
        LogAndDBG("Failed to parse JSON: " + parseResult.getErrorMessage());
        return juce::var();  // Return an empty var
    }

    return result;
  }

  bool saveCtrls(juce::File savePath, std::string audioInputPath) const {
    // Create a JSON array to hold each control's value
    juce::Array<juce::var> jsonCtrlsArray;

    // Iterate through each control in m_ctrls
    for (const auto& ctrlPair : m_ctrls) {
        auto ctrl = ctrlPair.second;

        // Check the type of ctrl and extract its value
        if (auto sliderCtrl = dynamic_cast<SliderCtrl*>(ctrl.get())) {
            // Slider control, use sliderCtrl->value
            jsonCtrlsArray.add(juce::var(sliderCtrl->value));
        } else if (auto textBoxCtrl = dynamic_cast<TextBoxCtrl*>(ctrl.get())) {
            // Text box control, use textBoxCtrl->value
            jsonCtrlsArray.add(juce::var(textBoxCtrl->value));
        } else if (auto numberBoxCtrl = dynamic_cast<NumberBoxCtrl*>(ctrl.get())) {
            // Number box control, use numberBoxCtrl->value
            jsonCtrlsArray.add(juce::var(numberBoxCtrl->value));
        } else if (auto toggleCtrl = dynamic_cast<ToggleCtrl*>(ctrl.get())) {
            // Toggle control, use toggleCtrl->value
            jsonCtrlsArray.add(juce::var(toggleCtrl->value));
        } else if (auto comboBoxCtrl = dynamic_cast<ComboBoxCtrl*>(ctrl.get())) {
            // Combo box control, use comboBoxCtrl->value
            jsonCtrlsArray.add(juce::var(comboBoxCtrl->value));
        } else if (auto audioInCtrl = dynamic_cast<AudioInCtrl*>(ctrl.get())) {
            // Audio in control, use audioInCtrl->value
            audioInCtrl->value = audioInputPath;
            jsonCtrlsArray.add(juce::var(audioInCtrl->value));
        } else if (auto midiInCtrl = dynamic_cast<MidiInCtrl*>(ctrl.get())) {
            midiInCtrl->value = audioInputPath;
            jsonCtrlsArray.add(juce::var(midiInCtrl->value));
        } else {
            // Unsupported control type or missing implementation
            LogAndDBG("Unsupported control type or missing implementation for control with ID: " + ctrl->id.toString());
            return false;
        }
    }

    // Convert the array to a JSON string
    juce::String jsonText = juce::JSON::toString(jsonCtrlsArray, true);  // true for human-readable

    // Write the JSON string to the specified file path
    if (!savePath.replaceWithText(jsonText)) {
        LogAndDBG("Failed to save controls to file: " + savePath.getFullPathName());
        return false;
    }

    return true;
  }

  juce::File m_cancel_flag_file {
    juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("webwave2wave_CANCEL")
  };
  juce::File m_status_flag_file {
    juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("webwave2wave_STATUS")
  };
  CtrlList m_ctrls;

  string m_url;
  string prefix_cmd;
  juce::File scriptPath;
};


// a timer that checks the status of the model and broadcasts a change if if there is one
class ModelStatusTimer : public juce::Timer,
                         public juce::ChangeBroadcaster {
public:
  ModelStatusTimer(std::shared_ptr<WebWave2Wave> model) : m_model(model) {
  }

  void timerCallback() override {
    // get the status of the model
    std::string status = m_model->getStatus();

    // if the status has changed, broadcast a change
    if (status != m_last_status) {
      m_last_status = status;
      sendChangeMessage();
    }
  }

private:
  std::shared_ptr<WebWave2Wave> m_model;
  std::string m_last_status;
};
