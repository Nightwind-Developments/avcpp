//
// Created by morgan on 12/01/21.
//

#include <iostream>
#include <set>
#include <map>
#include <memory>
#include <functional>

#include "av.h"
#include "ffmpeg.h"
#include "codec.h"
#include "packet.h"
#include "videorescaler.h"
#include "audioresampler.h"
#include "avutils.h"

// API2
#include "format.h"
#include "formatcontext.h"
#include "codec.h"
#include "codeccontext.h"


using namespace std;
using namespace av;

int setupInput(VideoDecoderContext &vdec, Stream vst, FormatContext &ictx, error_code ec, string uri, int count,
               ssize_t videoStream) {
    Dictionary dict = {
            {
                    "input_format", "mjpeg"
            }
    };

    ictx.openInput(uri, dict, InputFormat("v4l2"), ec);

    if (ec) {
        cerr << "Can't open input\n";
        return 1;
    }

    ictx.findStreamInfo();

    for (size_t i = 0; i < ictx.streamsCount(); ++i) {
        auto st = ictx.stream(i);
        if (st.mediaType() == AVMEDIA_TYPE_VIDEO) {
            cout << "here " << i << "   " << st.isVideo() << endl;
            videoStream = i;
            vst = st;
            break;
        }
    }

    if (vst.isNull()) {
        cerr << "Video stream not found\n";
        return 1;
    }

    if (vst.isValid()) {
        vdec = VideoDecoderContext(vst);


        cout << "width: " << vdec.width() << " height: " << vdec.height() << endl;
        vdec.setRefCountedFrames(true);
        Codec c = Codec();

        vdec.open(c, ec);
        cout << vdec.codec().name() << endl;
        if (ec.value()) {
            cerr << "Can't open codec\n" << ec << endl;
            return 1;
        }
    }
}

int setupOutput(VideoDecoderContext &vdec, error_code ec, string out, Stream vst, VideoEncoderContext &encoder,
                FormatContext &octx, Stream ost) {


    // Settings
    encoder.setWidth(vdec.width());
    encoder.setHeight(vdec.height());
    if (vdec.pixelFormat() > -1)
        encoder.setPixelFormat(vdec.pixelFormat());
    encoder.setTimeBase(Rational{1, 1000});
    encoder.setBitRate(vdec.bitRate());
    ost.setFrameRate(vst.frameRate());

    octx.openOutput(out, ec);
    if (ec) {
        cerr << "Can't open output\n";
        return 1;
    }

    Codec c2 = Codec();

    encoder.open(c2, ec);
    // encoder.setPixelFormat(c2.supportedPixelFormats().back());

    //cout<<"PF: "<<c2.supportedPixelFormats().back()<<endl;
    if (ec) {
        cerr << "Can't open encodec\n";
        return 1;
    }
    cout << "ererer" << endl;

    octx.dump();
    octx.writeHeader();
    octx.flush();
}

int record(VideoDecoderContext &vdec, FormatContext &ictx, error_code ec, VideoEncoderContext &encoder, int count,
           FormatContext &octx, ssize_t videoStream) {

    // Going to implement time interval
    auto start = std::chrono::steady_clock::now();
    auto end = std::chrono::steady_clock::now();
    Timestamp elapsed_seconds = end - start;
    int i = 0;

    while (elapsed_seconds.seconds() < 5) {
        cout << "secs: " << elapsed_seconds.seconds() << endl;
        //timer
        end = std::chrono::steady_clock::now();
        elapsed_seconds = end - start;
        double time = elapsed_seconds.seconds();
        if ((int) time != i)
            cout << i++ << endl;
        // READING
        Packet pkt = ictx.readPacket(ec);
        if (ec.value()) {
            clog << "Packet reading error: " << ec << ", " << ec.message() << endl;
            break;
        }

        bool flushDecoder = false;
        // !EOF
        if (pkt) {
            if (pkt.streamIndex() != videoStream) {
                continue;
            }

            clog << "Read packet: pts=" << pkt.pts() << ", dts=" << pkt.dts() << " / " << pkt.pts().seconds() << " / "
                 << pkt.timeBase() << " / st: " << pkt.streamIndex() << endl;
        } else {
            flushDecoder = true;
        }

        do {
            // DECODING
            auto frame = vdec.decode(pkt, ec);

            count++;
            //if (count > 200)
            //    break;

            bool flushEncoder = false;

            if (ec.value()) {
                cerr << "Decoding error: " << ec.value() << "    " << ec.message() << endl;
                return 1;
            } else if (!frame) {
                //cerr << "Empty frame\n";
                //flushDecoder = false;
                //continue;

                if (flushDecoder) {
                    flushEncoder = true;
                }
            }

            if (frame) {
                clog << "Frame: pts=" << frame.pts() << " / " << frame.pts().seconds() << " / " << frame.timeBase()
                     << ", " << frame.width() << "x" << frame.height() << ", size=" << frame.size() << ", ref="
                     << frame.isReferenced() << ":" << frame.refCount() << " / type: " << frame.pictureType() << endl;

                // Change timebase
                frame.setTimeBase(encoder.timeBase());
                frame.setStreamIndex(0);
                frame.setPictureType();

                clog << "Frame: pts=" << frame.pts() << " / " << frame.pts().seconds() << " / " << frame.timeBase()
                     << ", " << frame.width() << "x" << frame.height() << ", size=" << frame.size() << ", ref="
                     << frame.isReferenced() << ":" << frame.refCount() << " / type: " << frame.pictureType() << endl;
            }

            if (frame || flushEncoder) {
                do {
                    // Encode
                    Packet opkt = frame ? encoder.encode(frame, ec) : encoder.encode(ec);
                    if (ec) {
                        cerr << "Encoding error: " << ec << endl;
                        return 1;
                    } else if (!opkt) {
                        //cerr << "Empty packet\n";
                        //continue;
                        break;
                    }

                    // Only one output stream
                    opkt.setStreamIndex(0);

                    clog << "Write packet: pts=" << opkt.pts() << ", dts=" << opkt.dts() << " / "
                         << opkt.pts().seconds() << " / " << opkt.timeBase() << " / st: " << opkt.streamIndex() << endl;

                    octx.writePacket(opkt, ec);
                    if (ec) {
                        cerr << "Error write packet: " << ec << ", " << ec.message() << endl;
                        return 1;
                    }
                } while (flushEncoder);
            }

            if (flushEncoder)
                break;

        } while (flushDecoder);

        if (flushDecoder)
            break;
    }

    octx.writeTrailer();
}

int main(int argc, char **argv) {
    if (argc < 3)
        return 1;

    av::init();
    av::setFFmpegLoggingLevel(AV_LOG_DEBUG);

    string uri{argv[1]};
    string out{argv[2]};

    error_code ec;
    FormatContext ictx;
    VideoDecoderContext vdec;
    Stream vst;
    ssize_t videoStream = -1;

    int count = 0;


    OutputFormat ofrmt;
    FormatContext octx;

    ofrmt.setFormat(string(), out);
    octx.setFormat(ofrmt);

    Codec ocodec = findEncodingCodec(ofrmt);
    Stream ost = octx.addStream(ocodec);
    VideoEncoderContext encoder{ost};

    setupInput(vdec, vst, ictx, ec, uri, count, videoStream);
    setupOutput(vdec, ec, out, vst, encoder,
                octx, ost);
    return record(vdec, ictx, ec, encoder, count,
                  octx, videoStream);

}
