// One cold-start operation shared by all captures. Opening the application must
// not wait for a previous download's preflight or a filename/pause callback.
export class AppLauncher {
  constructor(request,version,{now=()=>Date.now(),sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms)),timeout=60000}={}){Object.assign(this,{request,version,now,sleep,timeout});this.pending=null;this.ready=null;this.checked=0;}
  ensure(){
    if(this.pending)return this.pending;
    if(this.ready && this.now()-this.checked<1500)return Promise.resolve(this.ready);
    this.pending=this.start().finally(()=>{this.pending=null;});return this.pending;
  }
  async start(){
    const deadline=this.now()+this.timeout;let opening=false,lastOpen=-Infinity,lastError;
    while(this.now()<deadline){
      try{
        if(!opening && this.now()-lastOpen>=5000){const result=await this.request({version:1,command:'open'});lastOpen=this.now();if(!result?.ok)throw Error(result?.error || 'Could not open Downshift');opening=result.state==='opening';}
        const answer=await this.request({version:1,command:'ping'});
        if(answer?.ok){if(answer.appVersion!==this.version)throw Object.assign(Error('Close the old Downshift window and open the updated Desktop shortcut, then retry.'),{versionMismatch:true});this.ready=answer;this.checked=this.now();return answer;}
        lastError=Error(answer?.error || 'Downshift is starting…');if(!opening)throw lastError;
      }catch(error){if(error.versionMismatch || !opening)throw error;lastError=error;}
      await this.sleep(400);
    }
    this.ready=null;throw lastError || Error('Downshift did not finish starting. Open the app to check startup.');
  }
}
