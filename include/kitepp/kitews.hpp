/*
 *   Copyright (c) 2021 Bhumit Attarde

 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.

 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.

 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <algorithm> //reverse
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring> //memcpy
#include <functional>
#include <ios>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "config.hpp"
#include "kiteppexceptions.hpp"
#include "responses.hpp"
#include "userconstants.hpp" //modes

#include "rapidjson/document.h"
#include "rapidjson/rapidjson.h"
#include "rapidjson/writer.h"
#include "rjutils.hpp"
#include <uWS/uWS.h>

namespace kiteconnect {

// To make sure doubles are parsed correctly
static_assert(std::numeric_limits<double>::is_iec559, "Requires IEEE 754 floating point!");

using std::string;
namespace rj = rapidjson;
namespace kc = kiteconnect;
namespace rju = kc::rjutils;

/**
 * @brief Used for accessing websocket interface of Kite API.
 *
 */
class kiteWS {

  public:
    // member variables

    // callbacks

    /**
     * @brief Called on successful connect.
     */
    std::function<void(kiteWS* ws)> onConnect;

    /**
     * @brief Called when ticks are received.
     */
    std::function<void(kiteWS* ws, const std::vector<kc::tick>& ticks)> onTicks;

    /**
     * @brief Called when an order update is received.
     */
    std::function<void(kiteWS* ws, const kc::postback& postback)> onOrderUpdate;

    /**
     * @brief Called when a message is received.
     */
    std::function<void(kiteWS* ws, const string& message)> onMessage;

    /**
     * @brief Called when connection is closed with an error or websocket server sends an error message.
     */
    std::function<void(kiteWS* ws, int code, const string& message)> onError;

    /**
     * @brief Called when an error occures while trying to connect.
     */
    std::function<void(kiteWS* ws)> onConnectError;

    /**
     * @brief Called when reconnection is being attempted.
     *
     * Auto reconnection:
     *
     * Auto reconnection is disabled by default and can be enabled by setting `enablereconnect` to `true` in `kiteWS`'s
     * constructor.
     * Auto reonnection mechanism is based on Exponential backoff algorithm in which next retry interval
     * will be increased exponentially. maxreconnectdelay and maxreconnecttries params can be used to tewak the
     * alogrithm where maxreconnectdelay is the maximum delay after which subsequent reconnection interval will become
     * constant and maxreconnecttries is maximum number of retries before its quiting reconnection.
     *
     */
    std::function<void(kiteWS* ws, unsigned int attemptCount)> onTryReconnect;

    /**
     * @brief Called when reconnect attempts exceed maximum reconnect attempts set by user i.e., when client is unable
     * to reconnect
     */
    std::function<void(kiteWS* ws)> onReconnectFail;

    /**
     * @brief Called when connection is closed.
     */
    std::function<void(kiteWS* ws, int code, const string& message)> onClose;

    // constructors & destructors

    /**
     * @brief Construct a new kiteWS object
     *
     * @param apikey API key
     * @param connecttimeout Connection timeout
     * @param enablereconnect Should be set to `true` for enabling reconnection
     * @param maxreconnectdelay Maximum reconnect delay for reconnection
     * @param maxreconnecttries Maximum reconnection attempts after which onReconnectFail will be called and no further
     * attempt to reconnect will be made.
     */
    kiteWS(const string& apikey, unsigned int connecttimeout = 5, bool enablereconnect = false,
        unsigned int maxreconnectdelay = 60, unsigned int maxreconnecttries = 30)
        : _apiKey(apikey), _connectTimeout(connecttimeout * 1000), _enableReconnect(enablereconnect),
          _maxReconnectDelay(maxreconnectdelay), _maxReconnectTries(maxreconnecttries),
          _hubGroup(_hub.createGroup<uWS::CLIENT>()) {};

    // x~kiteWS() {};

    // methods

    /**
     * @brief Set the API key
     *
     * @param arg
     */
    void setAPIKey(const string& arg) { _apiKey = arg; };

    /**
     * @brief get set API key
     *
     * @return string
     */
    string getAPIKey() const { return _apiKey; };

    /**
     * @brief Set the Access Token
     *
     * @param arg the string you want to set as access token
     *
     * @paragraph ex1 example
     * @snippet example2.cpp settting access token
     */
    void setAccessToken(const string& arg) { _accessToken = arg; };

    /**
     * @brief Get the Access Token set currently
     *
     * @return string
     */
    string getAccessToken() const { return _accessToken; };

    /**
     * @brief Connect to websocket server
     *
     */
    void connect() {
        _assignCallbacks();
        _connect();
    };

    /**
     * @brief Check if client is connected at present.
     *
     */
    bool isConnected() const { return _WS; };

    /**
     * @brief Get the last time heartbeat was received. Should be used in conjunction with `isConnected()` method.
     *
     * @return std::chrono::time_point<std::chrono::system_clock>
     */
    std::chrono::time_point<std::chrono::system_clock> getLastBeatTime() { return _lastBeatTime; };

    /**
     * @brief Start the client. Should always be called after `connect()`.
     *
     */
    void run() { _hub.run(); };

    /**
     * @brief Stop the client. Closes the connection if connected. Should be the last method to be called.
     *
     */
    void stop() {
        if (isConnected()) { _WS->close(); };
    };

    /**
     * @brief Subscribe instrument tokens.
     *
     * @param instrumentToks vector of instrument tokens to be subscribed.
     */
    void subscribe(const std::vector<int>& instrumentToks) {

        rj::Document req;
        req.SetObject();
        auto& reqAlloc = req.GetAllocator();
        rj::Value val;
        rj::Value toksArr(rj::kArrayType);

        val.SetString("subscribe", reqAlloc);
        req.AddMember("a", val, reqAlloc);

        for (const int tok : instrumentToks) { toksArr.PushBack(tok, reqAlloc); }
        req.AddMember("v", toksArr, reqAlloc);

        string reqStr = rju::_dump(req);
        if (isConnected()) {
            _WS->send(reqStr.data(), reqStr.size(), uWS::OpCode::TEXT);
            for (const int tok : instrumentToks) { _subbedInstruments[tok] = ""; };

        } else {
            throw kc::libException("Not connected to websocket server");
        };
    };

    /**
     * @brief unsubscribe instrument tokens.
     *
     * @param instrumentToks vector of instrument tokens to be unsubscribed.
     */
    void unsubscribe(const std::vector<int>& instrumentToks) {

        rj::Document req;
        req.SetObject();
        auto& reqAlloc = req.GetAllocator();
        rj::Value val;
        rj::Value toksArr(rj::kArrayType);

        val.SetString("unsubscribe", reqAlloc);
        req.AddMember("a", val, reqAlloc);

        for (const int tok : instrumentToks) { toksArr.PushBack(tok, reqAlloc); }
        req.AddMember("v", toksArr, reqAlloc);

        string reqStr = rju::_dump(req);
        if (isConnected()) {

            _WS->send(reqStr.data(), reqStr.size(), uWS::OpCode::TEXT);
            for (const int tok : instrumentToks) {
                auto it = _subbedInstruments.find(tok);
                if (it != _subbedInstruments.end()) { _subbedInstruments.erase(it); };
            };

        } else {

            throw kc::libException("Not connected to websocket server");
        };
    };

    /**
     * @brief Set the mode of instrument tokens.
     *
     * @param mode mode
     * @param instrumentToks vector of instrument tokens.
     */
    void setMode(const string& mode, const std::vector<int>& instrumentToks) {

        rj::Document req;
        req.SetObject();
        auto& reqAlloc = req.GetAllocator();
        rj::Value val;
        rj::Value valArr(rj::kArrayType);
        rj::Value toksArr(rj::kArrayType);

        val.SetString("mode", reqAlloc);
        req.AddMember("a", val, reqAlloc);

        val.SetString(mode.c_str(), mode.size(), reqAlloc);
        valArr.PushBack(val, reqAlloc);
        for (const int tok : instrumentToks) { toksArr.PushBack(tok, reqAlloc); }
        valArr.PushBack(toksArr, reqAlloc);
        req.AddMember("v", valArr, reqAlloc);

        string reqStr = rju::_dump(req);
        if (isConnected()) {

            _WS->send(reqStr.data(), reqStr.size(), uWS::OpCode::TEXT);
            for (const int tok : instrumentToks) { _subbedInstruments[tok] = mode; };

        } else {

            throw kc::libException("Not connected to websocket server");
        };
    };

  private:
    // For testing binary parsing
    friend class kWSTest_binaryParsingTest_Test;
    // member variables
    const string _connectURLFmt = "wss://ws.kite.trade/?api_key={0}&access_token={1}";
    string _apiKey;
    string _accessToken;
    // TODO make these into constants since they aren't used much
    const std::unordered_map<string, int> _segmentConstants = {

        { "nse", 1 },
        { "nfo", 2 },
        { "cds", 3 },
        { "bse", 4 },
        { "bfo", 5 },
        { "bsecds", 6 },
        { "mcx", 7 },
        { "mcxsx", 8 },
        { "indices", 9 },
    };
    std::unordered_map<int, string> _subbedInstruments; // instrument ID, mode

    uWS::Hub _hub;
    uWS::Group<uWS::CLIENT>* _hubGroup;
    uWS::WebSocket<uWS::CLIENT>* _WS = nullptr;
    const unsigned int _connectTimeout = 5000; // in ms

    const string _pingMessage = "";
    const unsigned int _pingInterval = 3000; // in ms

    const bool _enableReconnect = false;
    const unsigned int _initReconnectDelay = 2; // in seconds
    unsigned int _reconnectDelay = _initReconnectDelay;
    const unsigned int _maxReconnectDelay = 0; // in seconds
    unsigned int _reconnectTries = 0;
    const unsigned int _maxReconnectTries = 0; // in seconds
    std::atomic<bool> _isReconnecting { false };

    std::chrono::time_point<std::chrono::system_clock> _lastPongTime;
    std::chrono::time_point<std::chrono::system_clock> _lastBeatTime;

    // methods

    void _connect() {

        _hub.connect(FMT(_connectURLFmt, _apiKey, _accessToken), nullptr, {}, _connectTimeout, _hubGroup);
    };

    void _reconnect() {

        if (isConnected()) { return; };

        _isReconnecting = true;
        _reconnectTries++;

        if (_reconnectTries <= _maxReconnectTries) {

            std::this_thread::sleep_for(std::chrono::seconds(_reconnectDelay));
            _reconnectDelay = (_reconnectDelay * 2 > _maxReconnectDelay) ? _maxReconnectDelay : _reconnectDelay * 2;

            if (onTryReconnect) { onTryReconnect(this, _reconnectTries); };
            _connect();

            if (isConnected()) { return; };

        } else {

            if (onReconnectFail) { onReconnectFail(this); };
            _isReconnecting = false;
        };
    };

    void _processTextMessage(char* message, size_t length) {
        rj::Document res;
        rju::_parse(res, string(message, length));
        if (!res.IsObject()) { throw libException("Expected a JSON object"); };

        string type;
        rju::_getIfExists(res, type, "type");
        if (type.empty()) { throw kc::libException(FMT("Cannot recognize websocket message type {0}", type)); }

        if (type == "order" && onOrderUpdate) { onOrderUpdate(this, kc::postback(res["data"].GetObject())); }
        if (type == "message" && onMessage) { onMessage(this, string(message, length)); };
        if (type == "error" && onError) { onError(this, 0, res["data"].GetString()); };
    };

    // Convert bytesarray(array[start], arrray[end]) to number of type T
    template <typename T> T _getNum(const std::vector<char>& bytes, size_t start, size_t end) {

        T value;
        std::vector<char> requiredBytes(bytes.begin() + start, bytes.begin() + end + 1);

// clang-format off
        #ifndef WORDS_BIGENDIAN
        std::reverse(requiredBytes.begin(), requiredBytes.end());
        #endif
        // clang-format on

        std::memcpy(&value, requiredBytes.data(), sizeof(T));

        return value;
    };

    std::vector<std::vector<char>> _splitPackets(const std::vector<char>& bytes) {

        const int16_t numberOfPackets = _getNum<int16_t>(bytes, 0, 1);

        std::vector<std::vector<char>> packets;

        unsigned int packetLengthStartIdx = 2;
        for (int i = 1; i <= numberOfPackets; i++) {

            unsigned int packetLengthEndIdx = packetLengthStartIdx + 1;
            int16_t packetLength = _getNum<int16_t>(bytes, packetLengthStartIdx, packetLengthEndIdx);
            packetLengthStartIdx = packetLengthEndIdx + packetLength + 1;
            packets.emplace_back(bytes.begin() + packetLengthEndIdx + 1, bytes.begin() + packetLengthStartIdx);
        };

        return packets;
    };

    std::vector<kc::tick> _parseBinaryMessage(char* bytes, size_t size) {

        std::vector<std::vector<char>> packets = _splitPackets(std::vector<char>(bytes, bytes + size));
        if (packets.empty()) { return {}; };

        std::vector<kc::tick> ticks;
        for (const auto& packet : packets) {

            size_t packetSize = packet.size();
            int32_t instrumentToken = _getNum<int32_t>(packet, 0, 3);
            int segment = instrumentToken & 0xff;
            double divisor = (segment == _segmentConstants.at("cds")) ? 10000000.0 : 100.0;
            bool tradable = (segment == _segmentConstants.at("indices")) ? false : true;

            kc::tick Tick;

            Tick.isTradable = tradable;
            Tick.instrumentToken = instrumentToken;

            // LTP packet
            if (packetSize == 8) {

                Tick.mode = MODE_LTP;
                Tick.lastPrice = _getNum<int32_t>(packet, 4, 7) / divisor;

            } else if (packetSize == 28 || packetSize == 32) {
                // indices quote and full mode

                Tick.mode = (packetSize == 28) ? MODE_QUOTE : MODE_FULL;
                Tick.lastPrice = _getNum<int32_t>(packet, 4, 7) / divisor;
                Tick.OHLC.high = _getNum<int32_t>(packet, 8, 11) / divisor;
                Tick.OHLC.low = _getNum<int32_t>(packet, 12, 15) / divisor;
                Tick.OHLC.open = _getNum<int32_t>(packet, 16, 19) / divisor;
                Tick.OHLC.close = _getNum<int32_t>(packet, 20, 23) / divisor;
                // xTick.netChange = (Tick.lastPrice - Tick.OHLC.close) * 100 / Tick.OHLC.close;
                Tick.netChange = _getNum<int32_t>(packet, 24, 27) / divisor;

                // parse full mode with timestamp
                if (packetSize == 32) { Tick.timestamp = _getNum<int32_t>(packet, 28, 33); }

            } else if (packetSize == 44 || packetSize == 184) {
                // Quote and full mode

                Tick.mode = (packetSize == 44) ? MODE_QUOTE : MODE_FULL;
                Tick.lastPrice = _getNum<int32_t>(packet, 4, 7) / divisor;
                Tick.lastTradedQuantity = _getNum<int32_t>(packet, 8, 11);
                Tick.averageTradePrice = _getNum<int32_t>(packet, 12, 15) / divisor;
                Tick.volumeTraded = _getNum<int32_t>(packet, 16, 19);
                Tick.totalBuyQuantity = _getNum<int32_t>(packet, 20, 23);
                Tick.totalSellQuantity = _getNum<int32_t>(packet, 24, 27);
                Tick.OHLC.open = _getNum<int32_t>(packet, 28, 31) / divisor;
                Tick.OHLC.high = _getNum<int32_t>(packet, 32, 35) / divisor;
                Tick.OHLC.low = _getNum<int32_t>(packet, 36, 39) / divisor;
                Tick.OHLC.close = _getNum<int32_t>(packet, 40, 43) / divisor;

                Tick.netChange = (Tick.lastPrice - Tick.OHLC.close) * 100 / Tick.OHLC.close;

                // parse full mode
                if (packetSize == 184) {

                    Tick.lastTradeTime = _getNum<int32_t>(packet, 44, 47);
                    Tick.OI = _getNum<int32_t>(packet, 48, 51);
                    Tick.OIDayHigh = _getNum<int32_t>(packet, 52, 55);
                    Tick.OIDayLow = _getNum<int32_t>(packet, 56, 59);
                    Tick.timestamp = _getNum<int32_t>(packet, 60, 63);

                    unsigned int depthStartIdx = 64;
                    for (int i = 0; i <= 9; i++) {

                        kc::depthWS depth;
                        depth.quantity = _getNum<int32_t>(packet, depthStartIdx, depthStartIdx + 3);
                        depth.price = _getNum<int32_t>(packet, depthStartIdx + 4, depthStartIdx + 7) / divisor;
                        depth.orders = _getNum<int16_t>(packet, depthStartIdx + 8, depthStartIdx + 9);

                        (i >= 5) ? Tick.marketDepth.sell.emplace_back(depth) : Tick.marketDepth.buy.emplace_back(depth);
                        depthStartIdx = depthStartIdx + 12;
                    };
                };
            };

            ticks.emplace_back(Tick);
        };

        return ticks;
    };

    void _resubInstruments() {

        std::vector<int> LTPInstruments;
        std::vector<int> quoteInstruments;
        std::vector<int> fullInstruments;
        for (const auto& i : _subbedInstruments) {

            if (i.second == MODE_LTP) { LTPInstruments.push_back(i.first); };
            if (i.second == MODE_QUOTE) { quoteInstruments.push_back(i.first); };
            if (i.second == MODE_FULL) { fullInstruments.push_back(i.first); };
            // Set mode as quote if no mode was set
            if (i.second.empty()) { quoteInstruments.push_back(i.first); };
        };

        if (!LTPInstruments.empty()) { setMode(MODE_LTP, LTPInstruments); };
        if (!quoteInstruments.empty()) { setMode(MODE_QUOTE, quoteInstruments); };
        if (!fullInstruments.empty()) { setMode(MODE_FULL, fullInstruments); };
    };

    /*
    handled by uWS::Group::StartAutoPing() now. Code kept here as fallback.
    void _pingLoop() {

        while (!_stop) {

            std::cout << "Sending ping..\n";
            if (isConnected()) { _WS->ping(_pingMessage.data()); };
            std::this_thread::sleep_for(std::chrono::seconds(_pingInterval));

            if (_enableReconnect) {

                auto tmDiff =
                    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - _lastPongTime)
                        .count();

                if (tmDiff > _maxPongDelay) {
                    std::cout << FMT("Max pong exceeded.. tmDiff={0}\n", tmDiff);
                    if (isConnected()) { _WS->close(1006, "ping timed out"); };
                };
            };
        };
    };*/

    void _assignCallbacks() {

        _hubGroup->onConnection([&](uWS::WebSocket<uWS::CLIENT>* ws, uWS::HttpRequest req) {
            _WS = ws;
            // Not setting this time would prompt reconnecting immediately, even when conected since pongTime would be
            // far back or default
            _lastPongTime = std::chrono::system_clock::now();

            _reconnectTries = 0;
            _reconnectDelay = _initReconnectDelay;
            _isReconnecting = false;
            if (!_subbedInstruments.empty()) { _resubInstruments(); };
            if (onConnect) { onConnect(this); };
        });

        _hubGroup->onMessage([&](uWS::WebSocket<uWS::CLIENT>* ws, char* message, size_t length, uWS::OpCode opCode) {
            if (opCode == uWS::OpCode::BINARY && onTicks) {

                if (length == 1) {
                    // is a heartbeat
                    _lastBeatTime = std::chrono::system_clock::now();
                } else {
                    onTicks(this, _parseBinaryMessage(message, length));
                };

            } else if (opCode == uWS::OpCode::TEXT) {
                _processTextMessage(message, length);
            };
        });

        _hubGroup->onPong([&](uWS::WebSocket<uWS::CLIENT>* ws, char* message, size_t length) {
            _lastPongTime = std::chrono::system_clock::now();
        });

        _hubGroup->onError([&](void*) {
            if (onConnectError) { onConnectError(this); }
            // Close the non-responsive connection
            if (isConnected()) { _WS->close(1006); };
            if (_enableReconnect) { _reconnect(); };
        });

        _hubGroup->onDisconnection([&](uWS::WebSocket<uWS::CLIENT>* ws, int code, char* reason, size_t length) {
            _WS = nullptr;

            if (code != 1000) {
                if (onError) { onError(this, code, string(reason, length)); };
            };
            if (onClose) { onClose(this, code, string(reason, length)); };
            if (code != 1000) {
                if (_enableReconnect && !_isReconnecting) { _reconnect(); };
            };
        });

        _hubGroup->startAutoPing(_pingInterval, _pingMessage);
    };
};

} // namespace kiteconnect
