import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import App from './App.tsx'
import { createBrowserRouter } from "react-router";
import { RouterProvider } from "react-router/dom";
import { LogsPage } from './LogsPage'
import { Toaster } from 'react-hot-toast'

const router = createBrowserRouter([
  { path: '/', element: <App /> },
  { path: '/data', element: <LogsPage /> },
])

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <>
      <RouterProvider router={router} />
      <Toaster position="top-center" />
    </>
  </StrictMode>,
)
