//=============================================================================
/// Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
/// \author AMD Developer Tools Team
/// \file
/// \brief Simple class to detect if another copy of the current program is
/// running on the same system.
//==============================================================================

#ifndef _SINGLE_INSTANCE_H_
#define _SINGLE_INSTANCE_H_

#include "NamedMutex.h"

/// Simple class to detect if another copy of the current program is running on the same system.
class SingleInstance
{
protected:

    bool m_anotherInstanceRunning;  ///< Records if another instance of this program is running.

    NamedMutex m_mutex;             ///< Mutex held by the first instance of this class to be created

public:

    //--------------------------------------------------------------------------
    /// Constructor sets the name of the mutex and tries to open or create a new mutex instance
    /// \param strMutexName name of the mutex to use
    //--------------------------------------------------------------------------
    SingleInstance(const char* strMutexName)
    {
        // This always gets initialized to true. Only on the first instance of this class
        // will it get set to false in the  conditional below
        m_anotherInstanceRunning = true;

        // Try to open an existing mutex. If it succeeds it means a previous instance of this class
        // created the mutex so we know that another instance is running
        if (m_mutex.Open(strMutexName, false, true) == false)
        {
            // The mutex doesn't exist so create one
            m_mutex.OpenOrCreate(strMutexName, false, true);
            // Set this to false for our instance of this class in our process. We are the only instance of this application that is running.
            m_anotherInstanceRunning = false;
        }
    }

    //--------------------------------------------------------------------------
    /// Destructor
    //--------------------------------------------------------------------------
    ~SingleInstance()
    {
        m_mutex.Close();
    }

    //--------------------------------------------------------------------------
    /// Checks to see if another instance is running
    /// \return true if another instance is running; false otherwise
    //--------------------------------------------------------------------------
    bool IsProgramAlreadyRunning()
    {
        return m_anotherInstanceRunning;
    }
};

#endif //_SINGLE_INSTANCE_H_
