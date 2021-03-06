#include "testApp.h"

using namespace cv;
using namespace ofxCv;

//--------------------------------------------------------------
void testApp::setup() {

	isLive			= true;
	isTracking		= false;
	isTrackingHands	= false;
	isFiltering		= false;
	isRecording		= false;
	isCloud			= false;
	isCPBkgnd		= true;
	isMasking		= true;

	nearThreshold = 1900;
	farThreshold  = 3500;

	filterFactor = 0.1f;
    
    // open an outgoing connection to HOST:PORT
	sender.setup( HOST, PORT );

	setupRecording();

	ofBackground(0, 0, 0);

    syphonServer.setName("KinectDepth");

	contourFinder.setMinAreaRadius(4);
	contourFinder.setMaxAreaRadius(40);
    
    ofxOscMessage m;
    m.setAddress( "/kinectstarted" );
    sender.sendMessage( m );

}

void testApp::setupRecording(string _filename) {

#if defined (TARGET_OSX) //|| defined(TARGET_LINUX) // only working on Mac/Linux at the moment (but on Linux you need to run as sudo...)
	hardware.setup();				// libusb direct control of motor, LED and accelerometers
	hardware.setLedOption(LED_OFF); // turn off the led just for yacks (or for live installation/performances ;-)
#endif

	recordContext.setup();	// all nodes created by code -> NOT using the xml config file at all
	//recordContext.setupUsingXMLFile();
	recordDepth.setup(&recordContext);
	recordImage.setup(&recordContext);

	recordHandTracker.setup(&recordContext, 4);
	recordHandTracker.setSmoothing(filterFactor);		// built in openni hand track smoothing...
	recordHandTracker.setFilterFactors(filterFactor);	// custom smoothing/filtering (can also set per hand with setFilterFactor)...set them all to 0.1f to begin with

	recordContext.toggleRegisterViewport();
	recordContext.toggleMirror();
    
    // so they have the same size and type as cam
    for(int i = 0; i < N_PREVCAPTURES; i++){
        previous[i].allocate(recordDepth.getWidth(), recordDepth.getHeight(), OF_IMAGE_GRAYSCALE);
    }
    diff.allocate(recordDepth.getWidth(), recordDepth.getHeight(), OF_IMAGE_GRAYSCALE);
    invert.allocate(recordDepth.getWidth(), recordDepth.getHeight(), OF_IMAGE_COLOR);
    detect.allocate(recordDepth.getWidth(), recordDepth.getHeight(), OF_IMAGE_GRAYSCALE);

}

//--------------------------------------------------------------
void testApp::update(){

#ifdef TARGET_OSX // only working on Mac at the moment
	hardware.update();
#endif

	if (isLive) {

		// update all nodes
		recordContext.update();
		recordDepth.update();
		recordImage.update();

		// demo getting depth pixels directly from depth gen
		depthRangeMask.setFromPixels(recordDepth.getDepthPixels(nearThreshold, farThreshold),
									 recordDepth.getWidth(), recordDepth.getHeight(), OF_IMAGE_GRAYSCALE);

        depthRangeMask.mirror(false, true);
        // take the absolute difference of prev and cam and save it inside diff
		//absdiff(previous, depthRangeMask, diff);

        for(int i = N_PREVCAPTURES-1; i > 0; i--){
            previous[i] =  previous[i-1];
            add(previous[i], depthRangeMask, previous[i]);
        }
        copy(depthRangeMask, previous[0]);
        
        prev = previous[N_PREVCAPTURES-1];
        
        copy(prev, invert);
        invert.update();
        //invert the image
        int i = 0;
        while ( i < invert.getPixelsRef().size() ) {
            invert.getPixelsRef()[i] = 255 - invert.getPixelsRef()[i];
            i++;
        }
        invert.update();
        //invert.mirror(true, false);
        
        absdiff(previous[N_PREVCAPTURES-1], previous[N_PREVCAPTURES-2], diff);
        diff.update();
 		
		contourFinder.setThreshold(200);
		contourFinder.findContours(diff);
 
        i = 0;
        bool flagPerson = false;
        int x, y;
        while ( i < diff.getPixelsRef().size() ) {
            x = i - (int)(i / diff.getWidth()) * diff.getWidth();
            y = i / diff.getWidth();
            if(x > diff.getWidth() * 0.3 &&
               x < diff.getWidth() * 0.7 &&
               y < diff.getHeight() * 0.5){
                if( diff.getPixelsRef()[i] == 255){
                    flagPerson = true;
                }
                diff.getPixelsRef()[i] = 255 - diff.getPixelsRef()[i];
            }
            i++;
        }
        diff.update();
        if(flagPerson){
            ofxOscMessage m;
            m.setAddress( "/persondetected" );
            sender.sendMessage( m );
            ofLogWarning("sent person detected");
        }
        
		int n = contourFinder.size();
		for(int i = 0; i < n; i++) {
            cv::Point2f center = contourFinder.getCentroid(i);
            cv::Point2f velocity = contourFinder.getVelocity(i);
        
            ofxOscMessage m;
            m.setAddress( "/gust" );
            m.addIntArg( i );
            m.addIntArg( center.x );
            m.addIntArg( center.y );
            m.addIntArg( velocity.x );
            m.addIntArg( velocity.y );
            sender.sendMessage( m );

        }
	}

}

//--------------------------------------------------------------
void testApp::draw(){

	ofSetColor(255, 255, 255);

	glPushMatrix();

	if (isLive) {

		//recordDepth.draw(0,0,640,480);
		//recordImage.draw(640, 0, 640, 480);

		depthRangeMask.draw(0, 0, 320, 240);	// can use this with openCV to make masks, find contours etc when not dealing with openNI 'User' like objects

        prev.draw(320, 0, 320, 240);

        diff.draw(0, 240, 320, 240);
                
        invert.draw(320, 240, 320, 240);

        ofSetLineWidth(2);
        ofSetColor(255, 0, 0);
        contourFinder.draw();
        ofSetColor(0, 0, 255);
  		int n = contourFinder.size();
		for(int i = 0; i < n; i++) {
            cv::Point2f center = contourFinder.getCentroid(i);
            ofCircle(center.x, center.y, 3);
        }

        syphonServer.publishTexture(&invert.getTextureReference());

		if (isTracking) {

			if (isMasking) drawMasks();

		}
		if (isTrackingHands)
			recordHandTracker.drawHands();

	}
        
	glPopMatrix();

	ofSetColor(255, 255, 0);

	string statusPlay		= (string)(isLive ? "LIVE STREAM" : "PLAY STREAM");
	string statusRec		= (string)(!isRecording ? "READY" : "RECORDING");
	string statusHands		= (string)(isTrackingHands ? "TRACKING HANDS: " + (string)(isLive ? ofToString(recordHandTracker.getNumTrackedHands()) : ofToString(playHandTracker.getNumTrackedHands())) + ""  : "NOT TRACKING");
	string statusFilter		= (string)(isFiltering ? "FILTERING" : "NOT FILTERING");
	string statusFilterLvl	= ofToString(filterFactor);
	string statusSmoothHand = (string)(isLive ? ofToString(recordHandTracker.getSmoothing()) : ofToString(playHandTracker.getSmoothing()));
	string statusMask		= (string)(!isMasking ? "HIDE" : (isTracking ? "SHOW" : "YOU NEED TO TURN ON TRACKING!!"));
	string statusCloud		= (string)(isCloud ? "ON" : "OFF");
	string statusCloudData	= (string)(isCPBkgnd ? "SHOW BACKGROUND" : (isTracking ? "SHOW USER" : "YOU NEED TO TURN ON TRACKING!!"));

	string statusHardware;

#ifdef TARGET_OSX // only working on Mac at the moment
	ofPoint statusAccelerometers = hardware.getAccelerometers();
	stringstream	statusHardwareStream;

	statusHardwareStream
	<< "ACCELEROMETERS:"
	<< " TILT: " << hardware.getTiltAngle() << "/" << hardware.tilt_angle
	<< " x - " << statusAccelerometers.x
	<< " y - " << statusAccelerometers.y
	<< " z - " << statusAccelerometers.z;

	statusHardware = statusHardwareStream.str();
#endif

	stringstream msg;

	msg
	<< "    s : start/stop recording  : " << statusRec << endl
	<< "    p : playback/live streams : " << statusPlay << endl
	<< "    h : hand tracking         : " << statusHands << endl
	<< "    f : filter hands (custom) : " << statusFilter << endl
	<< "[ / ] : filter hands factor   : " << statusFilterLvl << endl
	<< "; / ' : smooth hands (openni) : " << statusSmoothHand << endl
	<< "    m : drawing masks         : " << statusMask << endl
	<< "    c : draw cloud points     : " << statusCloud << endl
	<< "    b : cloud user data       : " << statusCloudData << endl
	<< "- / + : nearThreshold         : " << ofToString(nearThreshold) << endl
	<< "< / > : farThreshold          : " << ofToString(farThreshold) << endl
	<< endl
	<< "FPS   : " << ofToString(ofGetFrameRate()) << "  " << statusHardware << endl;

	ofDrawBitmapString(msg.str(), 20, 20);

}

void testApp:: drawMasks() {
	glPushMatrix();
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
	allUserMasks.draw(640, 0, 640, 480);
	glDisable(GL_BLEND);
    glPopMatrix();
	user1Mask.draw(320, 480, 320, 240);
	user2Mask.draw(640, 480, 320, 240);
	
}

void testApp::drawPointCloud(ofxUserGenerator * user_generator, int userID) {

	glPushMatrix();

	int w = user_generator->getWidth();
	int h = user_generator->getHeight();

	glTranslatef(w, h/2, -500);
	ofRotateY(pointCloudRotationY);

	glBegin(GL_POINTS);

	int step = 1;

	for(int y = 0; y < h; y += step) {
		for(int x = 0; x < w; x += step) {
			ofPoint pos = user_generator->getWorldCoordinateAt(x, y, userID);
			if (pos.z == 0 && isCPBkgnd) continue;	// gets rid of background -> still a bit weird if userID > 0...
			ofColor color = user_generator->getWorldColorAt(x,y, userID);
			glColor4ub((unsigned char)color.r, (unsigned char)color.g, (unsigned char)color.b, (unsigned char)color.a);
			glVertex3f(pos.x, pos.y, pos.z);
		}
	}

	glEnd();

	glColor3f(1.0f, 1.0f, 1.0f);

	glPopMatrix();
}


//--------------------------------------------------------------
void testApp::keyPressed(int key){

	float smooth;

	switch (key) {
#ifdef TARGET_OSX // only working on Mac at the moment
		case 357: // up key
			hardware.setTiltAngle(hardware.tilt_angle++);
			break;
		case 359: // down key
			hardware.setTiltAngle(hardware.tilt_angle--);
			break;
#endif
			break;
		case 't':
		case 'T':
			isTracking = !isTracking;
			break;
		case 'h':
		case 'H':
			isTrackingHands = !isTrackingHands;
			break;
		case 'f':
		case 'F':
			isFiltering = !isFiltering;
			break;
		case 'm':
		case 'M':
			isMasking = !isMasking;
			break;
		case 'c':
		case 'C':
			isCloud = !isCloud;
			break;
		case 'b':
		case 'B':
			isCPBkgnd = !isCPBkgnd;
			break;
		case '0':
		case '>':
		case '.':
			farThreshold += 50;
			if (farThreshold > recordDepth.getMaxDepth()) farThreshold = recordDepth.getMaxDepth();
			break;
		case '<':
		case ',':
			farThreshold -= 50;
			if (farThreshold < 0) farThreshold = 0;
			break;

		case '+':
		case '=':
			nearThreshold += 50;
			if (nearThreshold > recordDepth.getMaxDepth()) nearThreshold = recordDepth.getMaxDepth();
			break;

		case '-':
		case '_':
			nearThreshold -= 50;
			if (nearThreshold < 0) nearThreshold = 0;
			break;
		case 'r':
			recordContext.toggleRegisterViewport();
			break;
		default:
			break;
	}
}


//--------------------------------------------------------------
void testApp::keyReleased(int key){

}

//--------------------------------------------------------------
void testApp::mouseMoved(int x, int y ){

	if (isCloud) pointCloudRotationY = x;

}

//--------------------------------------------------------------
void testApp::mouseDragged(int x, int y, int button){

}

//--------------------------------------------------------------
void testApp::mousePressed(int x, int y, int button){

}

//--------------------------------------------------------------
void testApp::mouseReleased(int x, int y, int button){

}

//--------------------------------------------------------------
void testApp::windowResized(int w, int h){

}

