#include "config.h"
#include "StelFileMgr.hpp"
#include <vector>
#include <set>
#include <string>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/random/linear_congruential.hpp>
	
using namespace std;
namespace fs = boost::filesystem;

StelFileMgr::StelFileMgr()
{
	// if the operating system supports user-level directories, the first place to look
	// for files is in there.
#if !defined(MACOSX) && !defined(MINGW32)
	// Linux & other POSIX-likes
	fs::path homeLocation(getenv("HOME"));
	if (!fs::is_directory(homeLocation))
	{
		// This will be the case if getenv failed or something else is weird
		cerr << "WARNING StelFileMgr::StelFileMgr: HOME env var refers to non-directory" << endl;
	}
	else 
	{
		homeLocation /= ".stellarium";
		fileLocations.push_back(homeLocation);
	}
#endif

	// TODO: the proper thing for Windows and OSX
	
	// OK, the main installation directory.  
#if !defined(MACOSX) && !defined(MINGW32)
	// For POSIX systems we use the value from the config.h filesystem
	fs::path installLocation(CONFIG_DATA_DIR);
	if (fs::exists(installLocation / CHECK_FILE))
	{
		fileLocations.push_back(installLocation);
		//configRootDir = installLocation;
	}
	else 
	{
		cerr << "WARNING StelFileMgr::StelFileMgr: could not find install location:" 
				<< installLocation.string() 
				<< " (we checked for " 
				<< CHECK_FILE
				<< ")."
				<< endl;
		installLocation = fs::current_path();
		if (fs::exists(installLocation / CHECK_FILE))
		{
			// cerr << "DEBUG StelFileMgr::StelFileMgr: found data files in current directory" << endl;
			fileLocations.push_back(installLocation);
			// configRootDir = installLocation;
		}		
		else 
		{
			cerr << "ERROR StelFileMgr::StelFileMgr: couldn't locate check file (" << CHECK_FILE << ") anywhere" << endl;	
		}
	}
#endif	
}

StelFileMgr::~StelFileMgr()
{
}

const fs::path StelFileMgr::findFile(const string& path, const FLAGS& flags)
{
	fs::path result;
	for(vector<fs::path>::iterator i = fileLocations.begin();
		   i != fileLocations.end();
		   i++)
	{		
		// cerr << "DEBUG StelFileMgr::findFile: " << path << " looking in " << (*i).string() << endl;
		// We set the return trigger to true, and then check each flag which is set
		// and the conditions for them, switching the return trigger to false if any
		// of the criteria fail.
		bool returnThisOne = true;
		if ( flags & NEW )
		{
			// if the NEW flag is set, we check to see if the parent is an existing directory
			// which is writable
			fs::path parent(*i / path / "..");
			if ( ! isWritable(parent.normalize()) || ! fs::is_directory(parent.normalize()) )
			{
				returnThisOne = false;	
			}						
		}
		else if ( fs::exists(*i / path) )
		{
			fs::path fullPath(*i / path);
			if ( (flags & WRITABLE) && ! isWritable(fullPath) )
				returnThisOne = false;
			
			if ( (flags & DIRECTORY) && ! fs::is_directory(fullPath) )
				returnThisOne = false;
			
			if ( (flags & FILE) && fs::is_directory(fullPath) )
				returnThisOne = false; 			
		}
		else
		{
			// doesn't exist and NEW flag wasn't requested
			returnThisOne = false;
		}
		
		if ( returnThisOne )
			return fs::path(*i / path);

	}
	
	cerr << "WARNING StelFileMgr::findFile: " << path << " file not found, throwing exception" << endl;
	throw(runtime_error("file not found"));
}
	
const set<string> StelFileMgr::listContents(const string& path, const StelFileMgr::FLAGS& flags)
{
	set<string> result;
	
	for ( vector<fs::path>::iterator li = fileLocations.begin();
              li != fileLocations.end();
	      li++ )
	{
		if ( fs::is_directory(*li / path) )
		{ 
			fs::directory_iterator end_iter;
			for ( fs::directory_iterator dir_itr( *li / path );
					    dir_itr != end_iter;
					    ++dir_itr )
			{
				// default is to return all objects in this directory
				bool returnThisOne = true;
			
				// but if we have flags set, that will filter the result
				fs::path fullPath(*li / path / dir_itr->leaf());
				if ( (flags & WRITABLE) && ! isWritable(fullPath) )
					returnThisOne = false;
			
				if ( (flags & DIRECTORY) && ! fs::is_directory(fullPath) )
					returnThisOne = false;
			
				// OK, add the ones we want to the result
				if ( returnThisOne )
					result.insert(dir_itr->leaf());
			}
		}
	}
	
	return result;
}

void StelFileMgr::setSearchPaths(const vector<fs::path> paths)
{
	fileLocations = paths;
}

const fs::path StelFileMgr::getDesktopDir(void)
{
	// TODO: Windows and Mac versions
	fs::path result(getenv("HOME"));
	result /= "Desktop";
	return result;
}

/**
 * Hopefully portable way to implement testing of write permission
 * to a file or directory.
 */
bool StelFileMgr::isWritable(const fs::path& path)
{
	bool result = false;
		
	if ( fs::exists ( path ) )
	{
		// For directories, we consider them writable if it is possible to create a file
		// inside them.  We will try to create a file called "stmp[randomnumber]"
		// (first without appending a random number, but if that exist, we'll re-try
		// 10 times with different random suffixes).
		// If this is possible we will delete it and return true, else we return false.
		// We'll use the boost random number library for portability
		if ( fs::is_directory ( path ) )
		{
			// cerr << "DEBUG StelFileMgr::isWritable: exists and is directory: " << path.string() << endl;
			boost::rand48 rnd(boost::int32_t(3141));
			int triesLeft = 50;
			ostringstream outs;
			
			outs << "stmp";
			fs::path testPath(path / outs.str());
					
			while(fs::exists(testPath) && triesLeft-- > 0)
			{
				// cerr << "DEBUG StelFileMgr::isWritable: test path " << testPath.string() << " already exists, trying another" << endl;
				outs.str("");
				outs << "stmp" << rnd();
				testPath = path / outs.str();
			}
			
			if (fs::exists(testPath))
			{
				// cerr << "DEBUG StelFileMgr::isWritable: I reall tried, but I can't make a unique non-existing file name to test" << endl;
				result = false;
			}
			else
			{
				// OK, we have a non-exiting testPath now.  We just call isWritable for that path
				// cerr << "DEBUG StelFileMgr::isWritable: testing non-existing file in dir: " << testPath.string() << endl;
				result = isWritable(testPath);
			}
		}
		else {
			// try to open writable (without damaging file)
			// cerr << "DEBUG StelFileMgr::isWritable: exists and is file: " << path.string() << endl;
			ofstream f ( path.native_file_string().c_str(), ios_base::out | ios_base::app );	
			if ( f )
			{
				result = true;
				f.close();
			}
		}
	}
	else
	{
		// try to create file to see if it's possible.
		// cerr << "DEBUG StelFileMgr::isWritable: does not exist: " << path.string() << endl;
		ofstream f ( path.native_file_string().c_str(), ios_base::out | ios_base::app );
		if ( f )
		{
			f.close();
			fs::remove (path);
			result = true;
		}
	}
	
	return result;
}

