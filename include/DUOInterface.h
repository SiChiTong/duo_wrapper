#pragma once
// Standard Includes
#include <string>
#include <stddef.h>
#include <memory>		//for std::shared_ptr
#include <functional>	//for std::function
#include <thread>		//Locking
#include <mutex>		//Locking
#include <ctime>		//Measuring execution time
//Include Duo Headers
#include "../include/duosdk/DUOLib.h"
#ifdef DUOMT	//Use DUO multithreading os singlethreading
#include "../include/duosdk/Dense3DMT.h"
#else
#include "../include/duosdk/Dense3D.h"
#endif // DUOMT

//Include openCV if found / chosen
#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#ifdef WITH_GPU
#include <opencv2/gpu/gpu.hpp>
#endif // WITH_GPU
#include <opencv2/calib3d/calib3d.hpp>

//Include visionworks if found / chosen
#ifdef WITH_NVX
#include <NVX/nvx.h>
#include <NVX/nvx_opencv_interop.hpp>
#endif // WITH_NVX



//Hardcoded defaults
#define GAIN		0.0f
#define EXPOSURE	100.0f
#define LEDS		50.0f
#define FPS			30.0f
#define WIDTH		752
#define HEIGHT		480
#define	BINNING		DUO_BIN_NONE


namespace duo
{
	class DUOInterface
	{
	public:
		DUOInterface(void);
		~DUOInterface(void);
		DUOInterface(const DUOInterface&) = delete;
		DUOInterface& operator=(const DUOInterface&) = delete;
		DUOInterface(DUOInterface&&) = delete;
		DUOInterface& operator=(DUOInterface&&) = delete;
		
		//Static defines
		static const int TWO_CAMERAS	= 2;
		static const int LEFT_CAM 		= 0;
		static const int RIGHT_CAM		= 1;
		static const std::string CAM_NAME;
		
		/*
		* 	@brief
		* 	This outside DUO API function ONLY, can access this DUOStereoDriver class 
		* 	private members since it is listed as a friend to this class. 
		* 	Be careful when using this. 
		*/
		friend void CALLBACK DUOCallback(const PDUOFrame pFrameData, void *pUserData);
		
		/*
		*	@brief
		*	Singleton Implementation to allow for only a single instance of this
		*	class. 
		*	-If GetInstance() is called more then once, it will return a pointer
		*	 to a already initialize (non-NULL) pSingleton object.
		*	-If GetInstance() is called for the first time, initialize the pSingleton
		*	 variable to a new DUOStereoDriver object.  
		*
		*	@return
		*	A pointer to the pSingleton private member object. Must implicitly ensure it is a reference
		*/
		static DUOInterface&	GetInstance(void){
			if (pSingleton == 0L)
			{
				pSingleton = new DUOInterface();
				std::cout << "New Singleton\n";
			}
			return *pSingleton;
		}
		
		/*
		*	@ brief
		*	Check if pSingleton object is not null, and if it is not null, call the 
		*	shutdownDUO() function FIRST, and then delete the pSingleton object.	
		*
		*/
		static void	DestroyInstance(void){
			if (pSingleton != 0L)
			{
				pSingleton->shutdownDUO();

				delete pSingleton;
				pSingleton = NULL;
				std::cout << "Singleton Deleted\n";
			}
		}

		bool initializeDUO(void);
		void startDUO(void);
		void shutdownDUO(void);
		
		//Duo parameter functions
		void SetExposure(double val){_exposure = val; SetDUOExposure(_duoInstance, val); }
		void SetGain(double val){_gain = val; SetDUOGain(_duoInstance, val); }
		void SetHFlip(bool val){_flipH = val; SetDUOHFlip(_duoInstance, val); }
		void SetVFlip(bool val){_flipV = val; SetDUOVFlip(_duoInstance, val); }
		void SetCameraSwap(bool val){_swap = val; SetDUOCameraSwap(_duoInstance, val); }
		void SetLedPWM(double val){_leds = val; SetDUOLedPWM(_duoInstance, val); }
		
		bool rectifyCV(const PDUOFrame pFrameData, void *pUserData, cv::Mat &leftR, cv::Mat &rightR);
		bool ReadYAML(std::string left, std::string right);
		bool ReadINI(std::string settings);
		bool WriteINI(std::string settings);
		void EnableCVSettings();
		//Camera characteristics storage
		struct openCVYaml{
			std::string	camera_name[TWO_CAMERAS];
			cv::Size resolution;
			std::string distortion_model;
			cv::Mat camera_matrix[TWO_CAMERAS];				//K
			cv::Mat distortion_coefficients[TWO_CAMERAS];	//D
			cv::Mat rectification_matrix[TWO_CAMERAS];		//R
			cv::Mat projection_matrix[TWO_CAMERAS];			//P
		};
		cv::Mat _dispN;
		
		//This is a callback function that will get passed the PDUOFrame when it is ready
		static std::function<void(const PDUOFrame pFrameData, void *pUserData)>	_extcallback;
		
		//Encapsulations
		bool GetOpencvCalib() const { return _opencvCalib; }
		bool GetRectifyOpencv() const { return _rectifyOpencv; }
		void SetRectifyOpencv(bool val) { _rectifyOpencv = val; }
		bool GetUseCUDA() const { return _useCUDA; }
		void SetUseCUDA(bool val) { _useCUDA = val; }
		
	protected:

		DUOInstance 		_duoInstance;
		DUOResolutionInfo 	_duoResolutionInfo;
		//Duo parameters
		double	_gain		= GAIN;
		double	_exposure	= EXPOSURE;
		double	_leds		= LEDS;
		bool	_flipH		= false;
		bool	_flipV		= false;
		bool	_swap		= false;
		size_t	_width		= WIDTH;
		size_t	_height		= HEIGHT;
		size_t	_fps		= FPS;
		int		_binning	= BINNING;
		char	_duoDeviceName[252];
		char	_duoDeviceSerialNumber[252];
		char	_duoDeviceFirmwareVersion[252];
		char	_duoDeviceFirmwareBuild[252];
		const DUOLEDSeq _ledSequence[2]; 
		
		//Library settings
		bool	_opencvCalib	= false;	//This indicates if openCV Calib files were found and successfully loaded
		bool	_opencvSettings	= false;	//
		bool	_rectifyOpencv	= false;	//This indicates if we should use openCV to rectify images, or inbuilt rectification.
		bool	_useCUDA		= false;	//
		bool	_calcDense3D	= false;
		
		//General variables
		bool			_duoInitialized = false;
		static			DUOInterface* pSingleton;
		static const	std::string CameraNames[TWO_CAMERAS]; // = {"left","right"};
		static			std::mutex _mutex;
		
		//OpenCV specifics
		static void on_trackbar(int, void*);
		std::shared_ptr<openCVYaml>	_cameraCalibCV;
		cv::Mat _mapL[2], _mapR[2];	//stores the rectification maps
#ifdef WITH_GPU
		cv::gpu::GpuMat _g_mapL[2], _g_mapR[2];	//stores the rectification maps
#endif // WITH_GPU
		
		
		//Trackbar variables
		//Trackbars for Dense3D Settings
		int _t_gain = GAIN;
		int _t_exposure = EXPOSURE;
		int _t_leds = LEDS;
		int _t_scale = 0;
		int _t_mode = 0;
		int _t_pre_filter = 1;
		int _t_num_disps = 2;
		int _t_SAD_window = 2;
		int _t_unique_r = 1;
		int _t_speckle_w = 0;
		int _t_speckle_r = 0;

		const int _t_gain_max = 100;
		const int _t_exposure_max = 100;
		const int _t_leds_max = 100;
		const int _t_scale_max = 3;
		const int _t_mode_max = 3;
		const int _t_pre_filter_max = 63;
		const int _t_num_disps_max = 16;
		const int _t_SAD_window_max = 10;
		const int _t_unique_r_max = 100;
		const int _t_speckle_w_max = 256;
		const int _t_speckle_r_max = 32;
	};
}



