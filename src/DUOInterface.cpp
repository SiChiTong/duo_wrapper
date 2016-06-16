#include "../include/DUOInterface.h"

#include <iostream>
#include  <ostream>

//These includes should be here if they don't need to be exposed outside of this lib


namespace duo
{
	const std::string DUOInterface::CameraNames[TWO_CAMERAS] = { "left", "right" };
	const std::string DUOInterface::CAM_NAME = "duo";
	
	//Static variables
	std::mutex DUOInterface::_mutex;
	DUOInterface* DUOInterface::pSingleton(0L);
	std::function<void(const PDUOFrame pFrameData, void *pUserData)>	DUOInterface::_extcallback = NULL;
	
	DUOInterface::DUOInterface(void) :
		_ledSequence {{ 100, 100, 100, },{ 0, 0, 0, },}	//One frame all on, one frame all off
	{
	}

	DUOInterface::~DUOInterface(void)
	{
		CloseDUO(_duoInstance);
	}
	
	void CALLBACK DUOCallback(const PDUOFrame pFrameData, void *pUserData)
	{
		if (pFrameData == NULL) return;	//Handle this better by restarting the duo maybe?
		std::lock_guard<std::mutex> lck(DUOInterface::_mutex);
		DUOInterface& 		_duo 	= DUOInterface::GetInstance(); 	// Using singleton to access DUOInterface, is it needed?
		
		//Check if rectification is needed, should be done outside of callback...
		if (_duo._rectifyOpencv)
		{
			cv::Mat	r_left, r_right;
			_duo.rectifyCV(pFrameData, pUserData, r_left, r_right);
			//Copy back into PDUOFrame, not sure if this is wise..., but works
			memcpy(pFrameData->leftData, r_left.ptr(0), r_left.rows*r_left.cols*sizeof(uint8_t));
			memcpy(pFrameData->rightData, r_right.ptr(0), r_right.rows*r_right.cols*sizeof(uint8_t));
		}
		
		if (DUOInterface::_extcallback)
			DUOInterface::_extcallback(pFrameData, pUserData);
	}

	bool DUOInterface::initializeDUO()
	{
		_duoInitialized = false;
		// Check if current resolution, framerate and binning is supported, otherwise error.
		if (EnumerateResolutions(&_duoResolutionInfo, 1, _width, _height, _binning, _fps))
		{
			// Attempt to open Duo Camera, otherwise error
			if (OpenDUO(&_duoInstance))
			{
				GetDUODeviceName(_duoInstance, _duoDeviceName);
				GetDUOSerialNumber(_duoInstance, _duoDeviceSerialNumber);
				GetDUOFirmwareVersion(_duoInstance, _duoDeviceFirmwareVersion);
				GetDUOFirmwareBuild(_duoInstance, _duoDeviceFirmwareBuild);
				SetDUOResolutionInfo(_duoInstance, _duoResolutionInfo);

				//Setup defaults
				SetDUOExposure(_duoInstance, _exposure);
				SetDUOGain(_duoInstance, _gain);
				SetDUOLedPWM(_duoInstance, _leds);
				SetDUOCameraSwap(_duoInstance, false);
				SetDUOUndistort(_duoInstance, true);
				
				//Check for openCV CalibFiles
				if (ReadYAML((CAM_NAME + "_" + CameraNames[LEFT_CAM]), (CAM_NAME + "_" + CameraNames[RIGHT_CAM])))
					_opencvCalib = true; //indicate this has been loaded correctly
				
				//Read settings from Camera
				if (ReadYAMLFromDuo())
					_duoCalib = true;
					
				//Check for openCV Setting storage
				if (ReadINI("cfg//duo_ini.yaml"))
					_opencvSettings = true;
				
				_duoInitialized = true;
			}
			else
			{
				_duoInitialized = false;
			}
		}
		else
		{
			_duoInitialized = false;
		}
		return _duoInitialized;
	}
	
	void DUOInterface::startDUO()
	{
		if (_duoInitialized)
		{
			// Check who should undistort...
			if (_rectifyOpencv && _opencvCalib)
			{
				std::cout <<  "Using open CV to rectify images" << std::endl;
				SetDUOUndistort(_duoInstance, false);
			}
			StartDUO(_duoInstance, DUOCallback, NULL);
			std::cout <<  "DUO Started." << std::endl;
		}	
	}

	void DUOInterface::shutdownDUO()
	{
		std::cout << "Shutting down DUO Camera." << std::endl;
		StopDUO(_duoInstance);
		//Save settings
		WriteINI("cfg/duo_ini.yaml");
	}
	
	//openCV functions
	bool DUOInterface::rectifyCV(const PDUOFrame pFrameData, void *pUserData, cv::Mat &leftR, cv::Mat &rightR){
		if (pFrameData == NULL) return 0;
		//std::clock_t begin = std::clock();
			if (_opencvCalib && _rectifyOpencv)
		{
			//Get images
			//std::clock_t begin = std::clock();
			cv::Mat left(cv::Size(WIDTH, HEIGHT), CV_8UC1, pFrameData->leftData);
			cv::Mat right(cv::Size(WIDTH, HEIGHT), CV_8UC1, pFrameData->rightData);
			if (GetUseCUDA())//upload GPU Mats
			{
#ifdef WITH_GPU
				cv::gpu::GpuMat l_dst, l_src, r_dst, r_src;
				l_src.upload(left);	//load mat to GPU
				r_src.upload(right);
				cv::gpu::remap(l_src, l_dst, _g_mapL[0], _g_mapL[1], cv::INTER_LINEAR);	//perform rectifications
				cv::gpu::remap(r_src, r_dst, _g_mapR[0], _g_mapR[1], cv::INTER_LINEAR);
				
				l_dst.download(leftR);
				r_dst.download(rightR); // download mat to ram
#endif
			}
			else
			{
			remap(left, leftR, _mapL[0], _mapL[1], cv::INTER_LINEAR);
			remap(right, rightR, _mapR[0], _mapR[1], cv::INTER_LINEAR);
			}
			//std::clock_t end = std::clock();
			//double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
			//std::cout << elapsed_secs << std::endl;
			return true;
		}
			else if (_rectifyOpencv)
		{
			//Currently not implemented
			return false;
		}
		return false;
	}
	
	bool DUOInterface::ReadYAML(std::string left, std::string right){
		
		left.insert(0, "cfg/");	//hardcoded cfg folder
		right.insert(0, "cfg/");
		left += ".yaml";	//Hardcoded .yaml extension
		right += ".yaml";
		cv::FileStorage fs_l( left, cv::FileStorage::READ);
		if (fs_l.isOpened())
		{
			fs_l["camera_name"] >> _cameraCalibCV.camera_name[LEFT_CAM];
			fs_l["image_width"] >> _cameraCalibCV.resolution.width;
			fs_l["image_height"] >> _cameraCalibCV.resolution.height;
			fs_l["distortion_model"] >> _cameraCalibCV.distortion_model;
			//Parameters
			fs_l["camera_matrix"] >> _cameraCalibCV.camera_matrix[LEFT_CAM];						//K
			fs_l["distortion_coefficients"] >> _cameraCalibCV.distortion_coefficients[LEFT_CAM];	//D
			fs_l["rectification_matrix"] >> _cameraCalibCV.rectification_matrix[LEFT_CAM];			//R
			fs_l["projection_matrix"] >> _cameraCalibCV.projection_matrix[LEFT_CAM];				//P
			//TODO: Implement check to see if read properly
			fs_l.release();
		}
		else
		{
			std::cout << "Could not open " << left << std::endl;
			return false;
		}
		//Do RHS
		cv::FileStorage fs_r(right, cv::FileStorage::READ);
		if (fs_r.isOpened())
		{
			fs_r["camera_name"] >> _cameraCalibCV.camera_name[RIGHT_CAM];
			//Parameters
			fs_r["camera_matrix"] >> _cameraCalibCV.camera_matrix[RIGHT_CAM];						//K
			fs_r["distortion_coefficients"] >> _cameraCalibCV.distortion_coefficients[RIGHT_CAM];	//D
			fs_r["rectification_matrix"] >> _cameraCalibCV.rectification_matrix[RIGHT_CAM];		//R
			fs_r["projection_matrix"] >> _cameraCalibCV.projection_matrix[RIGHT_CAM];				//P
			//TODO: Implement check to see if read properly
			fs_r.release();
		}
		else
		{
			std::cout << "Could not open " << right << std::endl;
			return false;
		}
		std::cout << "Found openCV Rectification settings\n";
		//Populate maps
		// Init rectify maps
		cv::fisheye::initUndistortRectifyMap(_cameraCalibCV.camera_matrix[LEFT_CAM],
			_cameraCalibCV.distortion_coefficients[LEFT_CAM],
			_cameraCalibCV.rectification_matrix[LEFT_CAM],
			_cameraCalibCV.projection_matrix[LEFT_CAM],
			_cameraCalibCV.resolution,
			CV_32FC1,
			_mapL[0],
			_mapL[1]);
		cv::fisheye::initUndistortRectifyMap(_cameraCalibCV.camera_matrix[RIGHT_CAM],
			_cameraCalibCV.distortion_coefficients[RIGHT_CAM],
			_cameraCalibCV.rectification_matrix[RIGHT_CAM],
			_cameraCalibCV.projection_matrix[RIGHT_CAM],
			_cameraCalibCV.resolution,
			CV_32FC1,
			_mapR[0],
			_mapR[1]);
#ifdef WITH_GPU
		if (GetUseCUDA())//upload GPU Mats
		{
			std::cout << "Using GPU, allocating rectification memory\n";
			for (size_t i = 0; i < 2; i++)
			{
				//ensure correct type
				_mapL[i].convertTo(_mapL[i], CV_32FC1);
				_mapR[i].convertTo(_mapR[i], CV_32FC1);
				if (_mapL[i].size != _mapR[i].size)
					std::cout << "Size issue\n";
				if (_mapL[i].type() != CV_32FC1)
					std::cout << "Type issue\n";
				_g_mapL[i].upload(_mapL[i]);
				_g_mapR[i].upload(_mapR[i]);
			}
		}
#endif //WITH_GPU
		return true;
	}
	
	bool DUOInterface::ReadYAMLFromDuo(){
		if (_duoInstance)
		{
			if (GetDUOCalibrationPresent(_duoInstance))	//Checks that the duo has a calibration
			{
				DUO_STEREO	stereoParam;
				GetDUOStereoParameters(_duoInstance, &stereoParam);
				calib_duo2cv(stereoParam, _cameraCalibDuo);	//Converts duo to openCV type
				return true;
			}
		}
		return false;
	}
	
	void DUOInterface::WriteCALIB(const openCVYaml& input, std::string prefix){
		std::string prefixR;
		prefix.insert(0, "cfg/");	//hardcoded cfg folder
		prefixR = prefix;
		prefix += input.camera_name[LEFT_CAM] + ".yaml";
		prefixR += input.camera_name[RIGHT_CAM] + ".yaml";
		cv::FileStorage fs_l(prefix, cv::FileStorage::WRITE);
		if (fs_l.isOpened())
		{
			fs_l << "camera_name" << input.camera_name[LEFT_CAM]
				<<	"image_width" << input.resolution.width
				<<	"image_height" << input.resolution.height
				<<	"distortion_model" << input.distortion_model
				<<	"camera_matrix"	<< input.camera_matrix[LEFT_CAM]
				<<	"distortion_coefficients" << input.distortion_coefficients[LEFT_CAM]
				<<	"rectification_matrix" << input.rectification_matrix[LEFT_CAM]
				<<	"projection_matrix" << input.projection_matrix[LEFT_CAM];
		}
		fs_l.release();
		cv::FileStorage fs_r(prefixR, cv::FileStorage::WRITE);
		if (fs_r.isOpened())
		{
			fs_r << "camera_name" << input.camera_name[RIGHT_CAM]
				<<	"image_width" << input.resolution.width
				<<	"image_height" << input.resolution.height
				<<	"distortion_model" << input.distortion_model
				<<	"camera_matrix"	<< input.camera_matrix[RIGHT_CAM]
				<<	"distortion_coefficients" << input.distortion_coefficients[RIGHT_CAM]
				<<	"rectification_matrix" << input.rectification_matrix[RIGHT_CAM]
				<<	"projection_matrix" << input.projection_matrix[RIGHT_CAM];
		}
		fs_r.release();
	}
	
	bool DUOInterface::ReadINI(std::string settings){
			//read in previous settings
		cv::FileStorage fs_s(settings, cv::FileStorage::READ);
		if (fs_s.isOpened())
		{
			int temp = 0;
			fs_s["_gain"]		>> _gain;
			fs_s["_exposure"]	>> _exposure;
			fs_s["_leds"]		>> _leds;
			fs_s["_flipH"]		>> _flipH;
			fs_s["_flipV"]		>> _flipV;
			fs_s["_swap"]		>> _swap;
			fs_s["_fps"]		>> temp; _fps = temp; temp = 0;	//Hack for opencv not liking size_t types
			fs_s["_binning"]	>> _binning;
		}
		else
		{
			fs_s.release();
			return false;
		}
		fs_s.release();
		return true;
	}
	
	bool DUOInterface::WriteINI(std::string settings){
		cv::FileStorage fs_s(settings, cv::FileStorage::WRITE);
		if (fs_s.isOpened())
		{
			fs_s << "gain"		<< _gain;
			fs_s << "exposure"	<< _exposure;
			fs_s << "leds"		<< _leds;
			fs_s << "flipH"		<< _flipH;
			fs_s << "flipV"		<< _flipV;
			fs_s << "swap"		<< _swap;
			fs_s << "fps"		<< (int )_fps;
			fs_s << "binning"	<< _binning;
		}
		else
		{
			fs_s.release();
			return false;
		}
		fs_s.release();
		return true;
	}

	void DUOInterface::EnableCVSettings() {
		cv::namedWindow("DuoCV Settings");	//Make window
		cv::createTrackbar("Gain", "DuoCV Settings", &_t_gain, _t_gain_max, DUOInterface::on_trackbar);
		cv::createTrackbar("Exposure", "DuoCV Settings", &_t_exposure, _t_exposure_max, DUOInterface::on_trackbar);
		cv::createTrackbar("LEDS", "DuoCV Settings", &_t_leds, _t_leds_max, DUOInterface::on_trackbar);
		if (_calcDense3D)
		{
			cv::createTrackbar("Scale", "DuoCV Settings", &_t_scale, _t_scale_max, DUOInterface::on_trackbar);
			cv::createTrackbar("mode", "DuoCV Settings", &_t_mode, _t_mode_max, DUOInterface::on_trackbar);
			cv::createTrackbar("pre_filter", "DuoCV Settings", &_t_pre_filter, _t_pre_filter_max, DUOInterface::on_trackbar);
			cv::createTrackbar("num_disps", "DuoCV Settings", &_t_num_disps, _t_num_disps_max, DUOInterface::on_trackbar);
			cv::createTrackbar("SAD_window", "DuoCV Settings", &_t_SAD_window, _t_SAD_window_max, DUOInterface::on_trackbar);
			cv::createTrackbar("unique_r", "DuoCV Settings", &_t_unique_r, _t_unique_r_max, DUOInterface::on_trackbar);
			cv::createTrackbar("speckle_w", "DuoCV Settings", &_t_speckle_w, _t_speckle_w_max, DUOInterface::on_trackbar);
			cv::createTrackbar("speckle_r", "DuoCV Settings", &_t_speckle_r, _t_speckle_r_max, DUOInterface::on_trackbar);
		}
	}
	
	void DUOInterface::calib_cv2duo(const openCVYaml& input, DUO_STEREO& output) {
		for (size_t i = 0; i < 9; i++)
		{
			output.M1[i] = input.camera_matrix[LEFT_CAM].at<double>(i);
			output.M2[i] = input.camera_matrix[RIGHT_CAM].at<double>(i);
			//output.R[i]; This is handled as a special case as 0 rotation matrix
			output.R1[i] = input.rectification_matrix[LEFT_CAM].at<double>(i);
			output.R2[i] = input.rectification_matrix[RIGHT_CAM].at<double>(i);
		}
		//TODO: Learn how to handle translating from 4 <-> distortion coeffecients...
		// http://docs.opencv.org/master/db/d58/group__calib3d__fisheye.html#gsc.tab=0 Fisheye has 4 ...
		// http://docs.opencv.org/2.4/doc/tutorials/calib3d/camera_calibration/camera_calibration.html Normal has 5 ...
		for (size_t i = 0; i < 4; i++)	//Play it safe and assume only 4 exist
		{
			output.D1[i] = input.distortion_coefficients[LEFT_CAM].at<double>(i);
			output.D2[i] = input.distortion_coefficients[RIGHT_CAM].at<double>(i);
		}
		//handle the 5th, seems correct with opencv documentation
		output.D1[4] = output.D2[4] = 0.0;
		
		//P incorporates T in opencv
		for (size_t i = 0; i < 12; i++)
		{
			output.P1[i] = input.projection_matrix[LEFT_CAM].at<double>(i);
			output.P2[i] = input.projection_matrix[RIGHT_CAM].at<double>(i);
		}
		//Needs to be extracted from openCV's P last colum...
		// Duo stores this as the distance in mm between cameras (Dx). the P vector stores this as Tx where
		// "Tx = -fx' * B, where B is the baseline between the cameras" aka B = Dx
		// Note, potentially Duo stores values in negative...
		output.T[0] = input.projection_matrix[RIGHT_CAM].at<double>(3) / -input.projection_matrix[RIGHT_CAM].at<double>(0); // Dx = Tx / -fx
		output.T[1] = input.projection_matrix[RIGHT_CAM].at<double>(7) / -input.projection_matrix[RIGHT_CAM].at<double>(5); // Dy = Ty / -fy
		output.T[2] = 0.0;
		// Currently unhandled...
		// http://stackoverflow.com/questions/27374970/q-matrix-for-the-reprojectimageto3d-function-in-opencv
		for (size_t i = 0; i < 16; i++)
		{
			output.Q[i] = 0;
		}
		//Insert zero rotation matrix
		double temp[] = {	1,0,0,
							0,1,0,
							0,0,1 };
		std::copy(temp, temp + 9, output.R);
		
	}
	
	void DUOInterface::calib_duo2cv(const DUO_STEREO& input, openCVYaml& output) {
		//Constant data
		size_t width = 0, height = 0;
		GetDUOFrameDimension(_duoInstance, &width, &height);
		//Convert to openCV Format
		output.camera_name[LEFT_CAM] = (CAM_NAME + "_" + CameraNames[LEFT_CAM]);
		output.camera_name[RIGHT_CAM] = (CAM_NAME + "_" + CameraNames[RIGHT_CAM]);
		output.resolution.width = width;
		output.resolution.height = height;
		output.distortion_model = "plumb_bob"; //Hardcoded untill new duo lib compatibility
		for (size_t i = 0; i < 9; i++)
		{
			output.camera_matrix[LEFT_CAM].at<double>(i) = input.M1[i];
			output.camera_matrix[RIGHT_CAM].at<double>(i) = input.M2[i];
			//input.R[i]; This is handled as a special case as 0 rotation matrix
			output.rectification_matrix[LEFT_CAM].at<double>(i) = input.R1[i];
			output.rectification_matrix[RIGHT_CAM].at<double>(i) = input.R2[i];
		}
		//TODO: Learn how to handle translating from 4 <-> 5 distortion coeffecients...
		// http://docs.opencv.org/master/db/d58/group__calib3d__fisheye.html#gsc.tab=0 Fisheye has 4 ...
		// http://docs.opencv.org/2.4/doc/tutorials/calib3d/camera_calibration/camera_calibration.html Normal has 5 ...
		for (size_t i = 0; i < 5; i++)	//Play it safe and assume only 4 exist
		{
			output.distortion_coefficients[LEFT_CAM].at<double>(i) = input.D1[i];
			output.distortion_coefficients[RIGHT_CAM].at<double>(i) = input.D2[i];
		}
		
		//P incorporates T in opencv
		for (size_t i = 0; i < 12; i++)
		{
			output.projection_matrix[LEFT_CAM].at<double>(i) = input.P1[i];
			output.projection_matrix[RIGHT_CAM].at<double>(i) = input.P2[i];
		}
		//Needs to be extracted from openCV's P last colum... //Maybe not.
//		for (size_t i = 0; i < 3; i++)
//		{
//			output.projection_matrix[RIGHT_CAM].at<double>((i * 4) + 3) = input.T[i];
//		}
	}
	
	void DUOInterface::on_trackbar(int, void*) {
		DUOInterface&	_duo = DUOInterface::GetInstance();
		//Camera Settings
		if (_duo._t_gain != _duo._gain){ _duo._gain = _duo._t_gain; _duo.SetGain(_duo._t_gain);}
		if (_duo._t_exposure != _duo._exposure){ _duo._exposure = _duo._t_exposure; _duo.SetExposure(_duo._t_exposure); }
		if (_duo._t_leds != _duo._leds){ _duo._leds = _duo._t_leds; _duo.SetLedPWM(_duo._t_leds); }
		if (_duo._calcDense3D)
		{
			
		}
	}
	
}	// end of duo namespace



