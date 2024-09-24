#include "GradioClient.h"

void GradioClient::extractKeyFromResponse(const juce::String& response,
                                          juce::String& responseKey,
                                          const juce::String& key,
                                          juce::String& error) const
{
    int dataIndex = response.indexOf(key);

    if (dataIndex == -1)
    {
        error = "Key " + key + " not found in response";
        return;
    }

    // Extract the portion after "key: "
    responseKey = response.substring(dataIndex + key.length()).trim();
}

void GradioClient::parseSpaceAddress(juce::String spaceAddress, SpaceInfo& spaceInfo)
{
    spaceInfo.userInput = spaceAddress;
    // Check if the URL is of Type 4 (localhost or gradio.live)
    if (spaceAddress.contains("localhost") || spaceAddress.contains("gradio.live")
        || spaceAddress.matchesWildcard("*.*.*.*:*", true))
    {
        spaceInfo.gradio = spaceAddress;
        spaceInfo.huggingface = spaceAddress;
        spaceInfo.status = SpaceInfo::Status::LOCALHOST;
        return;
    }
    juce::String user;
    juce::String model;

    juce::String huggingFaceBaseUrl = "https://huggingface.co/spaces/";
    if (spaceAddress.contains(huggingFaceBaseUrl))
    {
        // Remove the base URL part
        auto spacePath = spaceAddress.fromFirstOccurrenceOf(huggingFaceBaseUrl, false, false);

        // Split the path into user and model using '/'
        auto parts = juce::StringArray::fromTokens(spacePath, "/", "");

        if (parts.size() >= 2)
        {
            user = parts[0];
            model = parts[1];
            spaceInfo.status = SpaceInfo::Status::HUGGINGFACE;
        }
        else
        {
            // result.huggingface = spaceAddress;
            spaceInfo.error = "Detected huggingface.co URL but could not parse user and "
                              "model. Too few parts in "
                              + spaceAddress;
            spaceInfo.status = SpaceInfo::Status::ERROR;
            DBG(spaceInfo.error);
            return;
        }
    }
    else if (spaceAddress.contains("hf.space"))
    {
        // Remove the protocol part (e.g., "https://")
        auto withoutProtocol = spaceAddress.fromFirstOccurrenceOf("://", false, false);

        // Extract the subdomain part before ".hf.space/"
        auto subdomain = withoutProtocol.upToFirstOccurrenceOf(".hf.space", false, false);

        // Split the subdomain at the first hyphen
        auto firstHyphenIndex = subdomain.indexOfChar('-');
        if (firstHyphenIndex != -1)
        {
            user = subdomain.substring(0, firstHyphenIndex);
            model = subdomain.substring(firstHyphenIndex + 1);
            spaceInfo.status = SpaceInfo::Status::GRADIO;
        }
        else
        {
            // DBG("No hyphen found in the subdomain." << subdomain);
            // Even though the spaceAddress is supposed to be a gradio URL, we
            // return it as the huggingface URL because result.huggingface is
            // used for the  "Open Space URL" button in the error dialog box
            // result.huggingface = spaceAddress;
            spaceInfo.error = "Detected hf.space URL but could not parse user and model. No "
                              "hyphen found in the subdomain: "
                              + subdomain;
            spaceInfo.status = SpaceInfo::Status::ERROR;
            DBG(spaceInfo.error);
            return;
        }
    }
    // else if address is of the form user/model and doesn't contain http
    else if (spaceAddress.contains("/") && ! spaceAddress.contains("http"))
    {
        // Extract user and model
        auto parts = juce::StringArray::fromTokens(spaceAddress, "/", "");
        if (parts.size() == 2)
        {
            user = parts[0];
            model = parts[1];
            spaceInfo.status = SpaceInfo::Status::HUGGINGFACE;
        }
        else
        {
            // DBG("Invalid URL: " << spaceAddress);
            // result.huggingface = spaceAddress;
            spaceInfo.error = "Detected user/model URL but could not parse user and model. "
                              "Too many/few slashes in "
                              + spaceAddress;
            spaceInfo.status = SpaceInfo::Status::ERROR;
            DBG(spaceInfo.error);
            return;
        }
    }
    else
    {
        spaceInfo.error =
            "Invalid URL: " + spaceAddress + ". URL does not match any of the expected patterns.";
        spaceInfo.status = SpaceInfo::Status::ERROR;
        DBG(spaceInfo.error);
        return;
    }

    // Construct the gradio and HF URLs
    if (! user.isEmpty() && ! model.isEmpty())
    {
        model = model.replace("-", "_");
        spaceInfo.huggingface = "https://huggingface.co/spaces/" + user + "/" + model;
        model = model.replace("_", "-");
        spaceInfo.gradio = "https://" + user + "-" + model + ".hf.space";
        spaceInfo.userName = user;
        spaceInfo.modelName = model;
    }
    else
    {
        spaceInfo.error = "Unkown error while parsing the space address: " + spaceAddress;
        spaceInfo.status = SpaceInfo::Status::ERROR;
        DBG(spaceInfo.error);
        return;
    }
    LogAndDBG("User: " + user);
    LogAndDBG("Model: " + model);
    LogAndDBG("Gradio URL: " + spaceInfo.gradio);
    LogAndDBG("Huggingface URL: " + spaceInfo.huggingface);
    return;
}

void GradioClient::setSpaceInfo(const juce::String userProvidedSpaceAddress)
{
    parseSpaceAddress(userProvidedSpaceAddress, spaceInfo);
}

SpaceInfo GradioClient::getSpaceInfo() const { return spaceInfo; }

void GradioClient::uploadFileRequest(const juce::File& fileToUpload,
                                     juce::String& uploadedFilePath,
                                     juce::String& error) const
{
    juce::URL gradioEndpoint = spaceInfo.gradio;
    juce::URL uploadEndpoint = gradioEndpoint.getChildURL("upload");

    juce::StringPairArray responseHeaders;
    int statusCode = 0;
    juce::String mimeType = "audio/midi";

    // Use withFileToUpload to handle the multipart/form-data construction
    auto postEndpoint = uploadEndpoint.withFileToUpload("files", fileToUpload, mimeType);

    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                       // .withExtraHeaders("Accept: */*")
                       .withConnectionTimeoutMs(10000)
                       .withResponseHeaders(&responseHeaders)
                       .withStatusCode(&statusCode)
                       .withNumRedirectsToFollow(5)
                       .withHttpRequestCmd("POST");

    // Create the input stream for the POST request
    std::unique_ptr<juce::InputStream> stream(postEndpoint.createInputStream(options));

    if (stream == nullptr)
    {
        error = "Failed to create input stream for file upload request.";
        return;
    }

    juce::String response = stream->readEntireStreamAsString();

    // Check the status code to ensure the request was successful
    if (statusCode != 200)
    {
        error = "Request failed with status code: " + juce::String(statusCode);
        return;
    }

    // Parse the response
    juce::var parsedResponse = juce::JSON::parse(response);
    if (! parsedResponse.isObject())
    {
        error = "Failed to parse JSON response.";
        return;
    }

    juce::Array<juce::var>* responseArray = parsedResponse.getArray();
    if (responseArray == nullptr || responseArray->isEmpty())
    {
        error = "Parsed JSON does not contain the expected file path.";
        return;
    }

    // Get the first element in the array, which is the file path
    uploadedFilePath = responseArray->getFirst().toString();
    if (uploadedFilePath.isEmpty())
    {
        error = "File path not found in the response.";
        return;
    }

    DBG("File uploaded successfully, path: " + uploadedFilePath);
}

void GradioClient::makePostRequestForEventID(const juce::String endpoint,
                                             juce::String& eventID,
                                             juce::String& error,
                                             const juce::String jsonBody) const
{
    // Ensure that setSpaceInfo has been called before this method
    juce::URL gradioEndpoint = spaceInfo.gradio;
    juce::URL requestEndpoint = gradioEndpoint.getChildURL("call").getChildURL(endpoint);

    // Prepare the POST request
    // juce::String jsonBody = R"({"data": []})";
    juce::URL postEndpoint = requestEndpoint.withPOSTData(jsonBody);
    juce::StringPairArray responseHeaders;
    int statusCode = 0;
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                       .withExtraHeaders("Content-Type: application/json\r\nAccept: */*")
                       .withConnectionTimeoutMs(10000)
                       .withResponseHeaders(&responseHeaders)
                       .withStatusCode(&statusCode)
                       .withNumRedirectsToFollow(5)
                       .withHttpRequestCmd("POST");

    // Create the input stream for the POST request
    std::unique_ptr<juce::InputStream> stream(postEndpoint.createInputStream(options));

    if (stream == nullptr)
    {
        error = "Failed to create input stream for POST request to " + endpoint;
        return;
    }

    juce::String response = stream->readEntireStreamAsString();

    // Check the status code to ensure the request was successful
    if (statusCode != 200)
    {
        error = "Request to " + endpoint + " failed with status code: " + juce::String(statusCode);
        return;
    }

    // Parse the response
    juce::var parsedResponse = juce::JSON::parse(response);
    if (! parsedResponse.isObject())
    {
        error = "Failed to parse JSON response from " + endpoint;
        return;
    }

    juce::DynamicObject* obj = parsedResponse.getDynamicObject();
    if (obj == nullptr)
    {
        error = "Parsed JSON is not an object from " + endpoint;
        return;
    }

    eventID = obj->getProperty("event_id");
    if (eventID.isEmpty())
    {
        error = "event_id not found in the response from " + endpoint;
        return;
    }
}

void GradioClient::getResponseFromEventID(const juce::String callID,
                                          const juce::String eventID,
                                          juce::String& response,
                                          juce::String& error) const
{
    // Now we make a GET request to the endpoint with the event ID appended
    // The endpoint for the get request is the same as the post request with
    // /{eventID} appended
    juce::URL gradioEndpoint = spaceInfo.gradio;
    juce::URL getEndpoint =
        gradioEndpoint.getChildURL("call").getChildURL(callID).getChildURL(eventID);
    juce::StringPairArray responseHeaders;
    int statusCode = 0;
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       //    .withExtraHeaders("Content-Type: application/json\r\nAccept:
                       //    */*")
                       .withConnectionTimeoutMs(10000)
                       .withResponseHeaders(&responseHeaders)
                       .withStatusCode(&statusCode)
                       .withNumRedirectsToFollow(5);
    //  .withHttpRequestCmd ("POST");
    std::unique_ptr<juce::InputStream> stream(getEndpoint.createInputStream(options));

    if (stream == nullptr)
    {
        error = "Failed to create input stream for GET request.";
        return;
    }

    // Read the entire response from the stream
    response = stream->readEntireStreamAsString();
}

void GradioClient::getControls(juce::Array<juce::var>& ctrlList,
                               juce::DynamicObject& cardDict,
                               juce::String& error)
{
    juce::String callID = "controls";
    juce::String eventID;
    makePostRequestForEventID(callID, eventID, error);
    if (! error.isEmpty())
    {
        return;
    }

    juce::String response;
    getResponseFromEventID(callID, eventID, response, error);
    if (! error.isEmpty())
    {
        return;
    }

    // From the gradio app, we receive a JSON string
    // (see core.py in pyharp --> gr.Text(label="Controls"))
    // Extract the data portion from the response
    juce::String responseData;
    extractKeyFromResponse(response, responseData, "data: ", error);
    if (! error.isEmpty())
    {
        return;
    }

    // Parse the extracted JSON string
    juce::var parsedData;
    juce::Result parseResult = juce::JSON::parse(responseData, parsedData);

    if (! parsedData.isObject())
    {
        error = "Failed to parse the data portion of the received controls JSON.";
        return;
    }

    if (! parsedData.isArray())
    {
        error = "Parsed JSON is not an array.";
        return;
    }
    juce::Array<juce::var>* dataArray = parsedData.getArray();
    if (dataArray == nullptr)
    {
        error = "Parsed JSON is not an array 2.";
        return;
    }
    // Check if the first element in the array is a dict
    juce::DynamicObject* obj = dataArray->getFirst().getDynamicObject();
    if (obj == nullptr)
    {
        error = "First element in the array is not a dict.";
        return;
    }

    // Get the card and controls objects from the parsed data
    juce::DynamicObject* cardObj = obj->getProperty("card").getDynamicObject();

    if (cardObj == nullptr)
    {
        error = "Couldn't load the modelCard dict from the controls response.";
        return;
    }

    // Clear the existing properties in cardDict
    cardDict.clear();

    // Copy all properties from cardObj to cardDict
    for (auto& key : cardObj->getProperties())
    {
        cardDict.setProperty(key.name, key.value);
    }

    juce::Array<juce::var>* ctrlArray = obj->getProperty("ctrls").getArray();
    if (ctrlArray == nullptr)
    {
        error = "Couldn't load the controls array/list from the controls response.";
        return;
    }
    ctrlList = *ctrlArray;
}

void GradioClient::downloadFileFromURL(const juce::URL& fileURL, juce::String& downloadedFilePath, juce::String& error) const
{
    // Determine the local temporary directory for storing the downloaded file
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::String fileName = fileURL.getFileName();
    juce::File downloadedFile = tempDir.getChildFile(fileName);

    // Create input stream to download the file
    juce::StringPairArray responseHeaders;
    int statusCode = 0;
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs(10000)
                       .withResponseHeaders(&responseHeaders)
                       .withStatusCode(&statusCode)
                       .withNumRedirectsToFollow(5);

    std::unique_ptr<juce::InputStream> stream(fileURL.createInputStream(options));

    if (stream == nullptr)
    {
        error = "Failed to create input stream for file download request.";
        return;
    }

    // Check if the request was successful
    if (statusCode != 200)
    {
        error = "Request failed with status code: " + juce::String(statusCode);
        return;
    }

    // Create output stream to save the file locally
    std::unique_ptr<juce::FileOutputStream> fileOutput(downloadedFile.createOutputStream());

    if (fileOutput == nullptr || !fileOutput->openedOk())
    {
        error = "Failed to create output stream for file: " + downloadedFile.getFullPathName();
        return;
    }

    // Copy data from the input stream to the output stream
    fileOutput->writeFromInputStream(*stream, stream->getTotalLength());

    // Store the file path where the file was downloaded
    downloadedFilePath = downloadedFile.getFullPathName();

}